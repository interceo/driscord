#include "screen_receiver.hpp"

#include "log.hpp"
#include "utils/time.hpp"

ScreenReceiver::ScreenReceiver(int buffer_ms, int /*max_sync_gap_ms*/)
    : video_recv_(buffer_ms, 0),
      audio_recv_(std::make_shared<AudioReceiver>(buffer_ms, /*channels=*/2)) {}

void ScreenReceiver::push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len) {
    video_recv_.push_video_packet(peer_id, data, len);
}

void ScreenReceiver::push_audio_packet(const uint8_t* data, size_t len) {
    audio_recv_->push_packet(data, len);
}

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
            return held; // still frozen — audio hasn't caught up yet
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
    const int64_t diff     = video_ts - static_cast<int64_t>(audio_ts);

    if (diff > kDrainThresholdMs) {
        // Audio is severely lagging — drain stale frames so it catches up faster.
        const size_t drained = audio_recv_->drain_before(
            static_cast<uint64_t>(video_ts - kHoldThresholdMs));
        if (drained > 0) {
            LOG_INFO() << "[av-sync] drained " << drained
                       << " stale audio frames (video +" << diff << "ms ahead)";
        }
        // Re-read audio_ts after drain.
        const int64_t audio_ts2 = static_cast<int64_t>(audio_recv_->last_ts_ms());
        if (audio_ts2 > 0 && (video_ts - audio_ts2) > kHoldThresholdMs) {
            waiting_for_audio_ = true;
            LOG_INFO() << "[av-sync] holding video " << (video_ts - audio_ts2) << "ms ahead of audio";
            return frame; // display current frame frozen until audio catches up
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
