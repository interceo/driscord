#pragma once

#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_screen_session.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Transport;

class GoogleWebRtcClient final {
public:
    using FrameCallback = std::function<void(const std::string&,
        const driscord::media::DecodedVideoFrameView&)>;
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

    GoogleWebRtcClient(Transport& transport, Callbacks callbacks,
        std::vector<driscord::media::IceServerConfig> ice_servers = { });
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
    [[nodiscard]] std::vector<driscord::media::AudioDeviceInfo>
    input_devices() const;
    [[nodiscard]] std::vector<driscord::media::AudioDeviceInfo>
    output_devices() const;
    [[nodiscard]] bool set_input_device(const std::string& id);
    [[nodiscard]] bool set_output_device(const std::string& id);
    [[nodiscard]] bool audio_device_available() const;
    void set_peer_volume(const std::string& peer_id, float volume);
    [[nodiscard]] float peer_volume(const std::string& peer_id) const;
    void set_peer_muted(const std::string& peer_id, bool muted);
    [[nodiscard]] bool peer_muted(const std::string& peer_id) const;

    [[nodiscard]] std::string voice_stats_json() const;

    void init_screen();
    void deinit_screen();
    void join_stream(const std::string& peer_id);
    void leave_stream(const std::string& peer_id);
    void leave_streams();
    void remove_peer(const std::string& peer_id);
    void peer_stopped_streaming(const std::string& peer_id);

    [[nodiscard]] std::string video_targets_json() const;
    [[nodiscard]] Thumbnail grab_thumbnail(const std::string& target_json,
        int max_width,
        int max_height) const;
    [[nodiscard]] bool start_sharing(const std::string& target_json,
        int max_width,
        int max_height,
        int fps,
        bool share_audio,
        const std::string& audio_target = { });
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
