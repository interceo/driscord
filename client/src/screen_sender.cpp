#include "screen_sender.hpp"

#include "audio/system_audio_capture.hpp"
#include "log.hpp"
#include "utils/byte_utils.hpp"

#include <algorithm>
#include <cstring>

using namespace drist;

ScreenSender::ScreenSender() = default;
ScreenSender::~ScreenSender() { stop(); }

bool ScreenSender::start(
    const CaptureTarget& target,
    int max_w,
    int max_h,
    int fps,
    int base_bitrate_kbps,
    bool share_audio,
    SendCb on_video,
    SendCb on_audio
) {
    if (sharing_) {
        return false;
    }

    int enc_w = target.width & ~1;
    int enc_h = target.height & ~1;
    if (enc_w > max_w || enc_h > max_h) {
        float scale = std::min(static_cast<float>(max_w) / enc_w, static_cast<float>(max_h) / enc_h);
        enc_w = static_cast<int>(enc_w * scale) & ~1;
        enc_h = static_cast<int>(enc_h * scale) & ~1;
    }
    if (enc_w <= 0 || enc_h <= 0) {
        LOG_ERROR() << "invalid capture dimensions";
        return false;
    }

    if (!video_encoder_.init(enc_w, enc_h, fps, base_bitrate_kbps)) {
        LOG_ERROR() << "failed to init video encoder";
        return false;
    }

    fps_ = fps;
    base_bitrate_kbps_ = base_bitrate_kbps;
    on_video_ = std::move(on_video);
    on_audio_ = std::move(on_audio);
    send_frame_id_ = 0;

    encode_running_ = true;
    encode_thread_ = std::thread(&ScreenSender::encode_loop, this);

    screen_capture_ = ScreenCapture::create();
    if (!screen_capture_->start(fps, target, max_w, max_h, [this](ScreenCapture::Frame frame) {
            {
                std::scoped_lock lk(frame_mutex_);
                pending_frame_ = std::move(frame);
                frame_ready_ = true;
            }
            frame_cv_.notify_one();
        }))
    {
        LOG_ERROR() << "failed to start screen capture";
        encode_running_ = false;
        frame_cv_.notify_one();
        encode_thread_.join();
        video_encoder_.shutdown();
        return false;
    }

    sharing_ = true;

    if (share_audio && SystemAudioCapture::available()) {
        auto enc = std::make_unique<OpusEncode>();
        if (enc->init(opus::kSampleRate, kScreenAudioChannels, 128000, 2049 /* OPUS_APPLICATION_AUDIO */)) {
            system_audio_capture_ = SystemAudioCapture::create();
            if (system_audio_capture_ &&
                system_audio_capture_->start([this](const float* s, size_t f, int c) { on_audio_captured(s, f, c); }))
            {
                opus_encoder_ = std::move(enc);
                audio_capture_buf_.resize(static_cast<size_t>(opus::kFrameSize) * kScreenAudioChannels, 0.0f);
                audio_capture_pos_ = 0;
                audio_encode_buf_.resize(protocol::kAudioHeaderSize + opus::kMaxPacket);
                audio_send_seq_ = 0;
                sharing_audio_ = true;
                LOG_INFO() << "system audio capture started";
            } else {
                system_audio_capture_.reset();
                LOG_ERROR() << "failed to start system audio capture";
            }
        }
    }

    LOG_INFO() << "screen sharing started: " << target.name << " " << enc_w << "x" << enc_h << " @ " << fps << " fps";
    return true;
}

void ScreenSender::stop() {
    if (!sharing_) {
        return;
    }

    if (system_audio_capture_) {
        system_audio_capture_->stop();
        system_audio_capture_.reset();
    }
    opus_encoder_.reset();
    sharing_audio_ = false;

    if (screen_capture_) {
        screen_capture_->stop();
        screen_capture_.reset();
    }

    encode_running_ = false;
    frame_cv_.notify_one();
    if (encode_thread_.joinable()) {
        encode_thread_.join();
    }

    video_encoder_.shutdown();
    sharing_ = false;
    on_video_ = nullptr;
    on_audio_ = nullptr;

    LOG_INFO() << "screen sharing stopped";
}

void ScreenSender::encode_loop() {
    while (encode_running_) {
        ScreenCapture::Frame frame;
        {
            std::unique_lock lk(frame_mutex_);
            frame_cv_.wait_for(lk, std::chrono::milliseconds(100), [this] { return frame_ready_ || !encode_running_; });
            if (!frame_ready_) {
                continue;
            }
            frame = std::move(pending_frame_);
            frame_ready_ = false;
        }

        if (frame.data.empty() || !encode_running_) {
            continue;
        }

        if (frame.width != video_encoder_.width() || frame.height != video_encoder_.height()) {
            if (!video_encoder_.reinit(frame.width, frame.height, fps_, base_bitrate_kbps_)) {
                continue;
            }
            LOG_INFO() << "reinit video encoder: " << frame.width << "x" << frame.height;
        }

        const auto& encoded = video_encoder_.encode(frame.data.data(), frame.width, frame.height);
        if (encoded.empty()) {
            continue;
        }

        size_t full_size = protocol::kVideoHeaderSize + encoded.size();
        frame_buf_.resize(full_size);
        uint32_t video_send_ts = now_ms();
        write_u32_le(frame_buf_.data() + 0, static_cast<uint32_t>(frame.width));
        write_u32_le(frame_buf_.data() + 4, static_cast<uint32_t>(frame.height));
        write_u32_le(frame_buf_.data() + 8, video_send_ts);
        write_u32_le(frame_buf_.data() + 12, static_cast<uint32_t>(video_encoder_.measured_kbps()));
        std::memcpy(frame_buf_.data() + protocol::kVideoHeaderSize, encoded.data(), encoded.size());

        uint16_t fid = send_frame_id_++;
        size_t total_chunks = (frame_buf_.size() + protocol::kMaxChunkPayload - 1) / protocol::kMaxChunkPayload;
        if (total_chunks > 0xFFFF) {
            continue;
        }

        if (fid % 30 == 0) {
            LOG_INFO()
                << "[sync-send] video frame_id=" << fid << " sender_ts=" << video_send_ts << " size=" << encoded.size();
        }

        for (size_t ci = 0; ci < total_chunks; ++ci) {
            size_t offset = ci * protocol::kMaxChunkPayload;
            size_t chunk_len = std::min(protocol::kMaxChunkPayload, frame_buf_.size() - offset);

            send_buf_.resize(protocol::kChunkHeaderSize + chunk_len);
            write_u16_le(send_buf_.data() + 0, fid);
            write_u16_le(send_buf_.data() + 2, static_cast<uint16_t>(ci));
            write_u16_le(send_buf_.data() + 4, static_cast<uint16_t>(total_chunks));
            std::memcpy(send_buf_.data() + protocol::kChunkHeaderSize, frame_buf_.data() + offset, chunk_len);

            on_video_(send_buf_.data(), send_buf_.size());
        }
    }
}

void ScreenSender::on_audio_captured(const float* samples, size_t frames, int channels) {
    if (!opus_encoder_ || !on_audio_) {
        return;
    }

    size_t consumed = 0;
    const size_t stereo_frame_size = static_cast<size_t>(opus::kFrameSize) * kScreenAudioChannels;

    while (consumed < frames) {
        size_t remaining_slots = static_cast<size_t>(opus::kFrameSize) - audio_capture_pos_ / kScreenAudioChannels;
        size_t to_copy = std::min(remaining_slots, frames - consumed);

        for (size_t i = 0; i < to_copy; ++i) {
            size_t src_idx = (consumed + i) * channels;
            size_t dst_idx = audio_capture_pos_ + i * kScreenAudioChannels;
            audio_capture_buf_[dst_idx] = samples[src_idx];
            audio_capture_buf_[dst_idx + 1] = (channels >= 2) ? samples[src_idx + 1] : samples[src_idx];
        }

        audio_capture_pos_ += to_copy * kScreenAudioChannels;
        consumed += to_copy;

        if (audio_capture_pos_ >= stereo_frame_size) {
            uint8_t* opus_start = audio_encode_buf_.data() + protocol::kAudioHeaderSize;
            int bytes =
                opus_encoder_->encode(audio_capture_buf_.data(), opus::kFrameSize, opus_start, opus::kMaxPacket);
            if (bytes > 0) {
                uint32_t audio_send_ts = now_ms();
                write_u16_le(audio_encode_buf_.data(), audio_send_seq_);
                write_u32_le(audio_encode_buf_.data() + 2, audio_send_ts);
                if (audio_send_seq_ % 50 == 0) {
                    LOG_INFO() << "[sync-send] screen_audio seq=" << audio_send_seq_ << " sender_ts=" << audio_send_ts;
                }
                ++audio_send_seq_;
                on_audio_(audio_encode_buf_.data(), protocol::kAudioHeaderSize + static_cast<size_t>(bytes));
            }
            audio_capture_pos_ = 0;
        }
    }
}
