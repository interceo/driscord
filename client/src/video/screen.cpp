#include "screen.hpp"

#include "log.hpp"

#include <algorithm>
#include <cstring>

ScreenSender::~ScreenSender() { stop_sharing(); }

bool ScreenSender::start_sharing(
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

void ScreenSender::stop_sharing() {
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

void ScreenSender::on_audio_captured_(const float* samples, size_t frames, int channels) {
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

ScreenReceiver::ScreenReceiver(int buffer_ms, int /*max_sync_gap_ms*/)
    : video_recv_(buffer_ms, 0), audio_recv_(std::make_shared<AudioReceiver>(buffer_ms, /*channels=*/2)) {}

void ScreenReceiver::push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len) {
    video_recv_.push_video_packet(peer_id, data, len);
}

void ScreenReceiver::push_audio_packet(const uint8_t* data, size_t len) { audio_recv_->push_packet(data, len); }

const VideoJitter::Frame* ScreenReceiver::update() {
    const uint64_t audio_ts = audio_recv_->last_ts_ms();

    // ── While frozen waiting for audio ────────────────────────────────
    if (waiting_for_audio_) {
        const VideoJitter::Frame* held = video_recv_.current_frame();
        if (!held) {
            // No frame to show — just unfreeze.
            waiting_for_audio_ = false;
            return nullptr;
        }
        const int64_t video_ts = static_cast<int64_t>(utils::WallToMs(held->sender_ts));
        if (audio_ts == 0 || (video_ts - static_cast<int64_t>(audio_ts)) <= kHoldThresholdMs) {
            waiting_for_audio_ = false;
            // Fall through to pop a fresh frame now that audio caught up.
        } else {
            return held;  // still frozen — audio hasn't caught up yet
        }
    }

    // ── Normal advance ─────────────────────────────────────────────────
    const VideoJitter::Frame* frame = video_recv_.update();
    if (!frame) {
        return nullptr;
    }

    // No audio reference yet — pass through without sync.
    if (audio_ts == 0 || frame->sender_ts.time_since_epoch().count() == 0) {
        return frame;
    }

    const int64_t video_ts = static_cast<int64_t>(utils::WallToMs(frame->sender_ts));
    const int64_t diff = video_ts - static_cast<int64_t>(audio_ts);

    if (diff > kDrainThresholdMs) {
        // Audio is severely lagging — drain stale frames so it catches up faster.
        const size_t drained = audio_recv_->drain_before(static_cast<uint64_t>(video_ts - kHoldThresholdMs));
        if (drained > 0) {
            LOG_INFO() << "[av-sync] drained " << drained << " stale audio frames (video +" << diff << "ms ahead)";
        }
        // Re-read audio_ts after drain.
        const int64_t audio_ts2 = static_cast<int64_t>(audio_recv_->last_ts_ms());
        if (audio_ts2 > 0 && (video_ts - audio_ts2) > kHoldThresholdMs) {
            waiting_for_audio_ = true;
            LOG_INFO() << "[av-sync] holding video " << (video_ts - audio_ts2) << "ms ahead of audio";
            return frame;  // display current frame frozen until audio catches up
        }
    } else if (diff > kHoldThresholdMs) {
        // Video moderately ahead — freeze without draining.
        waiting_for_audio_ = true;
        LOG_INFO() << "[av-sync] holding video " << diff << "ms ahead of audio";
        return frame;
    }

    return frame;
}

void ScreenReceiver::set_keyframe_callback(std::function<void()> fn) {
    video_recv_.set_keyframe_callback(std::move(fn));
}

std::string ScreenReceiver::active_peer() const { return video_recv_.active_peer(); }
bool ScreenReceiver::active() const { return video_recv_.active(); }
int ScreenReceiver::measured_kbps() const { return video_recv_.measured_kbps(); }
VideoJitter::Stats ScreenReceiver::video_stats() const { return video_recv_.video_stats(); }
AudioReceiver::Stats ScreenReceiver::audio_stats() const { return audio_recv_->stats(); }

void ScreenReceiver::reset() {
    video_recv_.reset();
    audio_recv_->reset();
    waiting_for_audio_ = false;
}
