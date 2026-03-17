#include "screen_session.hpp"

ScreenSession::ScreenSession(
    int buf_ms,
    int max_sync_ms,
    SendCb send_video,
    std::function<void()> on_keyframe_req,
    SendCb send_screen_audio
)
    : receiver_(buf_ms, max_sync_ms)
    , send_video_(std::move(send_video))
    , send_screen_audio_(std::move(send_screen_audio)) {
    receiver_.set_keyframe_callback(std::move(on_keyframe_req));
}

bool ScreenSession::start_sharing(
    const CaptureTarget& target,
    int max_w,
    int max_h,
    int fps,
    int bitrate_kbps,
    int gop_size,
    bool share_audio
) {
    return sender_
        .start_sharing(target, max_w, max_h, fps, bitrate_kbps, gop_size, share_audio, send_video_, send_screen_audio_);
}

void ScreenSession::stop_sharing() {
    sender_.stop_sharing();
}

void ScreenSession::push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len) {
    receiver_.push_video_packet(peer_id, data, len);
}

void ScreenSession::push_audio_packet(const uint8_t* data, size_t len) {
    receiver_.push_audio_packet(data, len);
}

void ScreenSession::update() {
    if (const auto* frame = receiver_.update()) {
        std::string peer = receiver_.active_peer();
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
