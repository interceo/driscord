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
    using FrameCb  = std::function<void(const std::string&, const uint8_t*, int, int)>;

    Transport transport;
    AudioTransport audio_transport;
    VideoTransport video_transport;
    std::optional<ScreenSession> screen_session;

    DriscordCore();

    // ScreenSession lifecycle — wires core objects together.
    void init_screen_session(int buf_ms, int max_sync_ms);
    void deinit_screen_session();

    // Stream watching lifecycle.
    void join_stream(const std::string& peer_id);
    void leave_stream();

    // Called when a remote peer's video stream ended.
    void on_video_peer_stream_ended(const std::string& peer_id);

    // -- Callback setters (thread-safe) --
    void set_on_peer_joined(StringCb cb);
    void set_on_peer_left(StringCb cb);
    void set_on_new_streaming_peer(StringCb cb);
    void set_on_streaming_peer_removed(StringCb cb);
    void set_on_frame(FrameCb cb);
    void set_on_frame_removed(StringCb cb);

    // -- Transport facade --
    void add_turn_server(const std::string& url, const std::string& user, const std::string& pass);
    void connect(const std::string& url);
    void disconnect();
    bool connected() const;
    std::string local_id() const;
    std::string peers_json() const;

    // -- Audio facade --
    void audio_send(const uint8_t* data, int len);
    bool audio_start();
    void audio_stop();
    bool audio_deafened() const;
    void audio_set_deafened(bool d);
    float audio_master_volume() const;
    void audio_set_master_volume(float v);
    float audio_output_level() const;
    bool audio_self_muted() const;
    void audio_set_self_muted(bool m);
    float audio_input_level() const;
    void audio_on_peer_joined(const std::string& peer, int jitter_ms);
    void audio_on_peer_left(const std::string& peer);
    void audio_set_peer_volume(const std::string& peer, float vol);
    float audio_peer_volume(const std::string& peer) const;
    void audio_set_peer_muted(const std::string& peer, bool m);
    bool audio_peer_muted(const std::string& peer) const;
    void audio_set_screen_audio_receiver(const std::string& peer, bool has_screen);
    void audio_unset_screen_audio_receiver(const std::string& peer);
    void audio_add_screen_audio_to_mixer(const std::string& peer);
    void audio_remove_screen_audio_from_mixer(const std::string& peer);

    // -- Video facade --
    void video_set_watching(bool w);
    bool video_watching() const;
    void video_remove_streaming_peer(const std::string& peer);
    void video_send_keyframe_request();

    // -- Capture facade --
    bool capture_system_audio_available() const;
    std::string capture_list_targets_json() const;
    std::vector<uint8_t> capture_grab_thumbnail(
        const std::string& target_json,
        int max_w,
        int max_h
    );

    // -- Screen facade --
    bool screen_start_sharing(
        const std::string& target_json,
        int max_w,
        int max_h,
        int fps,
        int bitrate_kbps,
        bool share_audio
    );
    void screen_stop_sharing();
    bool screen_sharing() const;
    bool screen_sharing_audio() const;
    void screen_force_keyframe();
    void screen_update();
    std::string screen_active_peer() const;
    bool screen_active() const;
    void screen_reset();
    void screen_reset_audio();
    void screen_set_stream_volume(const std::string& peer, float vol);
    float screen_stream_volume() const;
    std::string screen_stats_json() const;

private:
    std::unordered_set<std::string> watched_peers_;

    // Callback storage
    std::mutex cb_mtx_;
    StringCb on_peer_joined_cb_;
    StringCb on_peer_left_cb_;
    StringCb on_new_streaming_peer_cb_;
    StringCb on_streaming_peer_removed_cb_;
    FrameCb  on_frame_cb_;
    StringCb on_frame_removed_cb_;
};
