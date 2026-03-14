#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/audio_engine.hpp"
#include "config.hpp"
#include "video/screen_capture.hpp"
#include "video/video_codec.hpp"
#include "video_renderer.hpp"
#include "voice_transport.hpp"

enum class AppState {
    Disconnected,
    Connecting,
    Connected,
};

class App {
public:
    explicit App(const Config& cfg);
    ~App();

    void update();

    void connect(const std::string& server_url);
    void disconnect();
    void toggle_mute();
    void set_volume(float vol);

    void start_sharing();
    void stop_sharing();
    bool sharing() const { return sharing_; }

    AppState state() const { return state_; }
    bool muted() const { return audio_.muted(); }
    float volume() const { return audio_.output_volume(); }
    float input_level() const { return audio_.input_level(); }
    float output_level() const { return audio_.output_level(); }
    std::string local_id() const { return transport_.local_id(); }
    const Config& config() const noexcept { return config_; }

    VideoRenderer& video_renderer() { return video_renderer_; }

    struct PeerView {
        std::string id;
        bool connected;
    };
    std::vector<PeerView> peers() const;

private:
    void on_video_packet(const std::string& peer_id, const uint8_t* data, size_t len);

    Config config_;
    AppState state_ = AppState::Disconnected;
    bool sharing_ = false;

    AudioEngine audio_;
    VoiceTransport transport_;
    VideoRenderer video_renderer_;

    std::unique_ptr<ScreenCapture> screen_capture_;
    VideoEncoder video_encoder_;

    struct PeerVideoState {
        VideoDecoder decoder;
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        bool dirty = false;
    };
    std::mutex video_mutex_;
    std::unordered_map<std::string, std::unique_ptr<PeerVideoState>> peer_video_;

    std::vector<uint8_t> send_buf_;
};
