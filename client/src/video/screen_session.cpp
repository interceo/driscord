#include "screen_session.hpp"

#include "log.hpp"

#include <algorithm>
#include <cstring>

ScreenSession::~ScreenSession() { stop_sharing(); }

// -------------------------------------------------------------------------
// Sender side
// -------------------------------------------------------------------------

bool ScreenSession::start_sharing(
    const CaptureTarget& target,
    int max_w,
    int max_h,
    int fps,
    int bitrate_kbps,
    bool share_audio,
    SendCb on_video,
    SendCb on_screen_audio
) {
    if (!video_sender_.start(fps, bitrate_kbps, std::move(on_video))) {
        return false;
    }

    screen_capture_ = ScreenCapture::create();
    if (!screen_capture_->start(
            fps,
            target,
            max_w,
            max_h,
            [this](ScreenCapture::Frame frame) { video_sender_.push_frame(std::move(frame)); }
        ))
    {
        video_sender_.stop();
        screen_capture_.reset();
        return false;
    }

    if (share_audio && on_screen_audio && SystemAudioCapture::available()) {
        auto enc = std::make_unique<OpusEncode>();
        if (enc->init(opus::kSampleRate, SystemAudioCapture::kChannels, 128000, 2049 /*OPUS_APPLICATION_AUDIO*/)) {
            auto cap = SystemAudioCapture::create();
            if (cap && cap->start([this](const float* s, size_t f, int c) { on_audio_captured_(s, f, c); })) {
                screen_audio_encoder_ = std::move(enc);
                system_audio_capture_ = std::move(cap);
                on_screen_audio_ = std::move(on_screen_audio);
                screen_audio_buf_.assign(static_cast<size_t>(opus::kFrameSize) * SystemAudioCapture::kChannels, 0.0f);
                screen_audio_encode_buf_.resize(protocol::AudioHeader::kWireSize + opus::kMaxPacket);
                screen_audio_pos_ = 0;
                screen_audio_seq_ = 0;
            }
        }
    }

    return true;
}

void ScreenSession::stop_sharing() {
    if (system_audio_capture_) {
        system_audio_capture_->stop();
        system_audio_capture_.reset();
    }
    screen_audio_encoder_.reset();
    on_screen_audio_ = nullptr;
    screen_audio_pos_ = 0;
    screen_audio_seq_ = 0;

    if (screen_capture_) {
        screen_capture_->stop();
        screen_capture_.reset();
    }
    video_sender_.stop();
}

void ScreenSession::reset() {
    video_recv_.reset();
    audio_recv_.reset();
}

void ScreenSession::on_audio_captured_(const float* samples, size_t frames, int channels) {
    if (!screen_audio_encoder_ || !on_screen_audio_) {
        return;
    }
    constexpr int kCh = SystemAudioCapture::kChannels;
    const size_t stereo_frame = static_cast<size_t>(opus::kFrameSize) * kCh;
    size_t consumed = 0;
    while (consumed < frames) {
        size_t remaining = static_cast<size_t>(opus::kFrameSize) - screen_audio_pos_ / kCh;
        size_t to_copy = std::min(remaining, frames - consumed);
        for (size_t i = 0; i < to_copy; ++i) {
            size_t src = (consumed + i) * channels;
            size_t dst = screen_audio_pos_ + i * kCh;
            screen_audio_buf_[dst] = samples[src];
            screen_audio_buf_[dst + 1] = (channels >= 2) ? samples[src + 1] : samples[src];
        }
        screen_audio_pos_ += to_copy * kCh;
        consumed += to_copy;
        if (screen_audio_pos_ >= stereo_frame) {
            uint8_t* out = screen_audio_encode_buf_.data() + protocol::AudioHeader::kWireSize;
            int bytes =
                screen_audio_encoder_->encode(screen_audio_buf_.data(), opus::kFrameSize, out, opus::kMaxPacket);
            if (bytes > 0) {
                protocol::AudioHeader ah{.seq = screen_audio_seq_++, .sender_ts = utils::WallNow()};
                ah.serialize(screen_audio_encode_buf_.data());
                on_screen_audio_(
                    screen_audio_encode_buf_.data(),
                    protocol::AudioHeader::kWireSize + static_cast<size_t>(bytes)
                );
            }
            screen_audio_pos_ = 0;
        }
    }
}