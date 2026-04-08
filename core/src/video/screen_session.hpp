#pragma once

#include "audio/audio.hpp"
#include "video/screen.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

class ScreenSession {
public:
    using SendCb    = std::function<void(const uint8_t*, const size_t)>;
    using OnFrameCb = std::function<
        void(const std::string& peer_id, const uint8_t* rgba, const int w, const int h)>;
    using OnRemovedCb = std::function<void(const std::string& peer_id)>;

    ScreenSession(
        int buf_ms,
        utils::Duration max_sync,
        SendCb send_video,
        std::function<void()> on_keyframe_req,
        SendCb send_screen_audio
    );
    ~ScreenSession() = default;

    ScreenSession(const ScreenSession&)            = delete;
    ScreenSession& operator=(const ScreenSession&) = delete;

    bool start_sharing(
        const ScreenCaptureTarget& target,
        const size_t max_w,
        const size_t max_h,
        const size_t fps,
        const size_t bitrate_kbps,
        bool share_audio
    );
    void stop_sharing();
    bool sharing() const { return sender_.sharing(); }
    bool sharing_audio() const { return sender_.sharing_audio(); }
    void force_keyframe() { sender_.force_keyframe(); }
    int sender_kbps() const { return sender_.sender_kbps(); }

    void push_video_packet(
        const std::string& peer_id,
        const utils::vector_view<const uint8_t> data,
        uint64_t frame_id
    );
    void push_audio_packet(
        const std::string& peer_id,
        const utils::vector_view<const uint8_t> data
    );

    void update();

    void set_on_frame(OnFrameCb cb);
    void set_on_frame_removed(OnRemovedCb cb);

    std::string active_peer() const { return receiver_.active_peer(); }
    bool active() const { return receiver_.active(); }
    int measured_kbps() const { return receiver_.measured_kbps(); }
    int last_width() const { return last_w_; }
    int last_height() const { return last_h_; }

    // Per-peer video receiver lifecycle (must be called before push_video_packet for that peer).
    void add_video_peer(const std::string& peer_id) { receiver_.add_video_peer(peer_id); }
    void remove_video_peer(const std::string& peer_id) { receiver_.remove_video_peer(peer_id); }

    // Per-peer audio receiver lifecycle (must be called before push_audio_packet for that peer).
    void add_audio_peer(const std::string& peer_id) { receiver_.add_audio_peer(peer_id); }
    void remove_audio_peer(const std::string& peer_id) { receiver_.remove_audio_peer(peer_id); }
    std::shared_ptr<AudioReceiver> audio_receiver(const std::string& peer_id) {
        return receiver_.audio_receiver(peer_id);
    }
    std::shared_ptr<const AudioReceiver> audio_receiver(const std::string& peer_id) const {
        return receiver_.audio_receiver(peer_id);
    }

    VideoReceiver::Stats video_stats() const { return cached_video_stats_; }
    AudioReceiver::Stats audio_stats() const { return cached_audio_stats_; }
    std::string stats_json() const;

    void reset();
    void reset_audio();

private:
    ScreenSender sender_;
    ScreenReceiver receiver_;

    SendCb send_video_;
    SendCb send_screen_audio_;

    std::unordered_set<std::string> last_peers_;
    int last_w_ = 0;
    int last_h_ = 0;

    utils::Duration max_sync_;

    using Clock = std::chrono::steady_clock;
    Clock::time_point last_stats_refresh_{};
    VideoReceiver::Stats cached_video_stats_{};
    AudioReceiver::Stats cached_audio_stats_{};

    mutable std::mutex cb_mutex_;
    OnFrameCb on_frame_cb_;
    OnRemovedCb on_frame_removed_cb_;
};
