#pragma once

#include "transport.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class GoogleWebRtcClient;

// One STUN or TURN server as the user configures it. A plain value type so the
// media stack's types stay behind GoogleWebRtcClient.
struct IceServer {
    std::string url;
    std::string username;
    std::string password;
};

// Business/API boundary used by the Qt client. Media implementation details
// stay behind GoogleWebRtcClient and native Google WebRTC tracks.
class DriscordCore {
public:
    using StringCb = std::function<void(const std::string&)>;
    using WatchRejectedCb = std::function<void(
        const std::string&, signaling::WatchRejectReason)>;
    using FrameCb = std::function<void(const std::string&, const uint8_t*, int, int)>;

    Transport transport;

    // Without ICE servers the client offers host candidates only, which
    // reaches an SFU just when a routable address sits on its interface.
    explicit DriscordCore(std::vector<IceServer> ice_servers = { });
    ~DriscordCore();

    DriscordCore(const DriscordCore&) = delete;
    DriscordCore& operator=(const DriscordCore&) = delete;

    void init_screen_session();
    void deinit_screen_session();
    void join_stream(const std::string& peer_id);
    void leave_stream(const std::string& peer_id);
    void leave_stream();

    void set_on_peer_joined(StringCb cb);
    void set_on_peer_left(StringCb cb);
    void set_on_peer_identity(
        std::function<void(const std::string&, const std::string&)> cb);
    void set_on_frame(FrameCb cb);
    void set_on_frame_removed(StringCb cb);
    void set_on_streaming_started(StringCb cb);
    void set_on_streaming_stopped(StringCb cb);
    void set_on_watch_rejected(WatchRejectedCb cb);

    void voice_start();
    void voice_stop();
    void voice_set_muted(bool muted);
    [[nodiscard]] bool voice_muted() const;
    void audio_set_deafened(bool deafened);
    [[nodiscard]] bool audio_deafened() const;
    void audio_set_master_volume(float volume);
    [[nodiscard]] float audio_master_volume() const;
    [[nodiscard]] std::string audio_input_devices_json() const;
    [[nodiscard]] std::string audio_output_devices_json() const;
    [[nodiscard]] bool audio_set_input_device(const std::string& id);
    [[nodiscard]] bool audio_set_output_device(const std::string& id);
    // False when no working audio device was available and the media stack
    // fell back to a silent dummy: sessions connect and negotiate audio, but
    // the user is neither heard nor hears anyone. Surface this in the UI.
    [[nodiscard]] bool audio_device_available() const;
    void audio_set_peer_volume(const std::string& peer, float volume);
    [[nodiscard]] float audio_peer_volume(const std::string& peer) const;
    void audio_set_peer_muted(const std::string& peer, bool muted);
    [[nodiscard]] bool audio_peer_muted(const std::string& peer) const;
    // Voice transport counters and the round trip to the SFU, as JSON.
    [[nodiscard]] std::string voice_stats_json() const;

    [[nodiscard]] std::string capture_audio_list_targets_json() const;
    [[nodiscard]] std::string capture_video_list_targets_json() const;

    struct ThumbnailResult {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };
    [[nodiscard]] ThumbnailResult capture_grab_thumbnail(
        const std::string& target_json,
        int max_width,
        int max_height);

    // `audio_target` is a playback device id from
    // capture_audio_list_targets_json(); empty means the default sink.
    [[nodiscard]] bool screen_start_sharing(const std::string& target_json,
        int max_width,
        int max_height,
        int fps,
        bool share_audio,
        const std::string& audio_target = { });
    void screen_stop_sharing();
    [[nodiscard]] bool screen_sharing() const;
    void screen_set_local_preview_enabled(bool enabled);
    [[nodiscard]] std::string screen_stats_json(
        const std::string& peer) const;
    void screen_set_stream_volume(const std::string& peer, float volume);
    [[nodiscard]] float screen_stream_volume(const std::string& peer) const;

private:
    void emit_frame(const std::string& peer,
        const uint8_t* rgba,
        int width,
        int height);
    void emit_frame_removed(const std::string& peer);

    std::unique_ptr<GoogleWebRtcClient> media_;
    mutable std::mutex cb_mtx_;
    StringCb on_peer_joined_cb_;
    StringCb on_peer_left_cb_;
    std::function<void(const std::string&, const std::string&)>
        on_peer_identity_cb_;
    FrameCb on_frame_cb_;
    StringCb on_frame_removed_cb_;
    StringCb on_streaming_started_cb_;
    StringCb on_streaming_stopped_cb_;
    WatchRejectedCb on_watch_rejected_cb_;
};
