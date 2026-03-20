#include "screen_session.hpp"

#include <chrono>

ScreenSession::ScreenSession(
    int buf_ms,
    utils::Duration max_sync,
    SendCb send_video,
    std::function<void()> on_keyframe_req,
    SendCb send_screen_audio
)
    : receiver_(
          buf_ms,
          static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(max_sync).count())
      )
    , send_video_(std::move(send_video))
    , send_screen_audio_(std::move(send_screen_audio))
    , max_sync_(max_sync) {
    receiver_.set_keyframe_callback(std::move(on_keyframe_req));
}

bool ScreenSession::start_sharing(
    const CaptureTarget& target,
    const size_t max_w,
    const size_t max_h,
    const size_t fps,
    const size_t bitrate_kbps,
    bool share_audio
) {
    return sender_.start_sharing(
        target,
        max_w,
        max_h,
        fps,
        bitrate_kbps,
        share_audio,
        send_video_,
        send_screen_audio_
    );
}

void ScreenSession::stop_sharing() {
    sender_.stop_sharing();
}

void ScreenSession::push_video_packet(
    const std::string& peer_id,
    const utils::vector_view<const uint8_t> data
) {
    receiver_.push_video_packet(peer_id, data);
}

void ScreenSession::push_audio_packet(const utils::vector_view<const uint8_t> data) {
    receiver_.push_audio_packet(data);
}

void ScreenSession::update() {
    if (max_sync_ > utils::Duration::zero()) {
        receiver_.evict_old(max_sync_);

        if (receiver_.video_primed() && receiver_.audio_primed()) {
            const auto v_ts = receiver_.video_front_effective_ts();
            const auto a_ts = receiver_.audio_front_effective_ts();
            if (v_ts && a_ts) {
                const auto
                    drift_ms = std::chrono::duration_cast<std::chrono::milliseconds>(*a_ts - *v_ts);
                const auto frame_ms = std::chrono::duration_cast<
                    std::chrono::milliseconds>(receiver_.video_frame_duration());
                const auto
                    max_sync_ms = std::chrono::duration_cast<std::chrono::milliseconds>(max_sync_);
                if (drift_ms > max_sync_ms + frame_ms) {
                    receiver_.evict_video_before(*a_ts - frame_ms);
                }
            }
        }
    }

    const auto now = Clock::now();
    if (now - last_stats_refresh_ >= std::chrono::milliseconds(500)) {
        cached_video_stats_ = receiver_.video_stats();
        cached_audio_stats_ = receiver_.audio_stats();
        last_stats_refresh_ = now;
    }

    if (const auto* frame = receiver_.update()) {
        const std::string& peer = frame->peer_id;
        if (!peer.empty()) {
            if (!last_peer_.empty() && last_peer_ != peer) {
                std::scoped_lock lk(cb_mutex_);
                if (on_frame_removed_cb_) {
                    on_frame_removed_cb_(last_peer_);
                }
            }
            last_w_ = frame->width;
            last_h_ = frame->height;
            {
                std::scoped_lock lk(cb_mutex_);
                if (on_frame_cb_) {
                    on_frame_cb_(peer, frame->rgba.data(), frame->width, frame->height);
                }
            }
            last_peer_ = peer;
        }
    }
    if (!last_peer_.empty() && !receiver_.active()) {
        {
            std::scoped_lock lk(cb_mutex_);
            if (on_frame_removed_cb_) {
                on_frame_removed_cb_(last_peer_);
            }
        }
        last_peer_.clear();
    }
}

void ScreenSession::set_on_frame(OnFrameCb cb) {
    std::scoped_lock lk(cb_mutex_);
    on_frame_cb_ = std::move(cb);
}

void ScreenSession::set_on_frame_removed(OnRemovedCb cb) {
    std::scoped_lock lk(cb_mutex_);
    on_frame_removed_cb_ = std::move(cb);
}

void ScreenSession::reset() {
    receiver_.reset();
    last_peer_.clear();
}

void ScreenSession::reset_audio() {
    receiver_.audio_receiver()->reset();
}
