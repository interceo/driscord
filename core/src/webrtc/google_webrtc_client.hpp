#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Transport;

// Application-facing coordinator for the two native WebRTC PeerConnections.
// It is a class because it owns asynchronous session/capture/playout lifetimes;
// packet senders, receivers, codecs and jitter buffers deliberately remain
// native Google WebRTC objects rather than Driscord wrapper classes.
class GoogleWebRtcClient final {
public:
    using FrameCallback = std::function<void(const std::string&, const uint8_t*, int, int)>;
    using PeerCallback = std::function<void(const std::string&)>;

    struct Callbacks {
        FrameCallback on_frame;
        PeerCallback on_frame_removed;
    };

    struct Thumbnail {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };

    explicit GoogleWebRtcClient(Transport& transport, Callbacks callbacks);
    ~GoogleWebRtcClient();

    GoogleWebRtcClient(const GoogleWebRtcClient&) = delete;
    GoogleWebRtcClient& operator=(const GoogleWebRtcClient&) = delete;

    void start_voice();
    void stop_voice();
    void set_muted(bool muted);
    [[nodiscard]] bool muted() const;
    void set_deafened(bool deafened);
    [[nodiscard]] bool deafened() const;
    void set_master_volume(float volume);
    [[nodiscard]] float master_volume() const;
    void set_peer_volume(const std::string& peer_id, float volume);
    [[nodiscard]] float peer_volume(const std::string& peer_id) const;
    void set_peer_muted(const std::string& peer_id, bool muted);
    [[nodiscard]] bool peer_muted(const std::string& peer_id) const;

    void init_screen();
    void deinit_screen();
    void join_stream(const std::string& peer_id);
    void leave_stream(const std::string& peer_id);
    void leave_streams();
    void remove_peer(const std::string& peer_id);
    [[nodiscard]] bool watching() const;

    [[nodiscard]] std::string video_targets_json() const;
    [[nodiscard]] Thumbnail grab_thumbnail(const std::string& target_json,
        int max_width,
        int max_height) const;
    [[nodiscard]] bool start_sharing(const std::string& target_json,
        int max_width,
        int max_height,
        int fps,
        bool share_audio);
    void stop_sharing();
    [[nodiscard]] bool sharing() const;
    void set_local_preview_enabled(bool enabled);
    void set_stream_volume(const std::string& peer_id, float volume);
    [[nodiscard]] float stream_volume(const std::string& peer_id) const;
    [[nodiscard]] std::string screen_stats_json(
        const std::string& peer_id) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
