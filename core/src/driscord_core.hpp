#pragma once

#include "audio_transport.hpp"
#include "transport.hpp"
#include "video/screen_session.hpp"
#include "video_transport.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

// Owns all core objects and contains business logic that coordinates them.
class DriscordCore {
public:
    using StringCb = std::function<void(const std::string&)>;
    using FrameCb = std::function<void(const std::string&, const uint8_t*, int, int)>;

    Transport transport;
    AudioTransport audio_transport;
    VideoTransport video_transport;
    std::optional<ScreenSession> screen_session;

    DriscordCore();

    // ScreenSession lifecycle — wires core objects together.
    void init_screen_session();
    void deinit_screen_session();

    // Stream watching lifecycle.
    void join_stream(const std::string& peer_id);
    void leave_stream();

    // Called when a remote peer's video stream ended.
    void on_video_peer_stream_ended(const std::string& peer_id);

    // -- Callback setters (thread-safe) --
    void set_on_peer_joined(StringCb cb);
    void set_on_peer_left(StringCb cb);
    void set_on_peer_identity(std::function<void(const std::string&, const std::string&)> cb);
    void set_on_new_streaming_peer(StringCb cb);
    void set_on_streaming_peer_removed(StringCb cb);
    void set_on_frame(FrameCb cb);
    void set_on_frame_removed(StringCb cb);
    void set_on_streaming_started(StringCb cb);
    void set_on_streaming_stopped(StringCb cb);

    // -- Transport (non-trivial) --
    std::string peers_json() const;
    void set_local_username(const std::string& username);

    // -- Audio (cross-subsystem) --
    void audio_set_screen_audio_receiver(const std::string& peer, bool has_screen);

    // -- Video --
    void video_set_watching(bool w);

    // -- Capture (JSON serialisation logic) --
    std::string capture_audio_list_targets_json() const;
    std::string capture_video_list_targets_json() const;

    struct ThumbnailResult {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };
    ThumbnailResult capture_grab_thumbnail(const std::string& target_json,
        int max_w,
        int max_h);

    // -- Screen (cross-subsystem / multi-step) --
    utils::Expected<void, VideoError> screen_start_sharing(const std::string& target_json,
        int max_w,
        int max_h,
        int fps,
        bool share_audio);
    void screen_stop_sharing();
    void screen_set_stream_volume(const std::string& peer, float vol);
    float screen_stream_volume() const;

private:
    std::unordered_set<std::string> watched_peers_;

    // Callback storage
    std::mutex cb_mtx_;
    StringCb on_peer_joined_cb_;
    StringCb on_peer_left_cb_;
    std::function<void(const std::string&, const std::string&)> on_peer_identity_cb_;
    StringCb on_new_streaming_peer_cb_;
    StringCb on_streaming_peer_removed_cb_;
    FrameCb on_frame_cb_;
    StringCb on_frame_removed_cb_;
    StringCb on_streaming_started_cb_;
    StringCb on_streaming_stopped_cb_;
};
