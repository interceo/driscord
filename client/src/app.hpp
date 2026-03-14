#pragma once

#include <chrono>
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

inline constexpr const char* kPreviewPeerId = "__preview__";

enum class AppState {
    Disconnected,
    Connecting,
    Connected,
};

enum class StreamQuality : int {
    Source = 0,
    HD_720,
    FHD_1080,
    QHD_1440,
    Count,
};

struct StreamPreset {
    const char* label;
    int width;
    int height;
};

inline constexpr StreamPreset kStreamPresets[] = {
    {"Source", 0, 0},
    {"720p", 1280, 720},
    {"1080p", 1920, 1080},
    {"1440p", 2560, 1440},
};
static_assert(static_cast<int>(StreamQuality::Count) == sizeof(kStreamPresets) / sizeof(kStreamPresets[0]));
inline constexpr int kStreamPresetCount = static_cast<int>(StreamQuality::Count);

enum class FrameRate : int {
    FPS_15 = 0,
    FPS_30,
    FPS_60,
    Count,
};

inline constexpr int kFpsValues[] = {15, 30, 60};
static_assert(static_cast<int>(FrameRate::Count) == sizeof(kFpsValues) / sizeof(kFpsValues[0]));
inline constexpr int kFpsOptionCount = static_cast<int>(FrameRate::Count);

inline constexpr int fps_value(FrameRate fr) { return kFpsValues[static_cast<int>(fr)]; }

struct StreamStats {
    int width = 0;
    int height = 0;
    int measured_kbps = 0;
};

class App {
public:
    explicit App(const Config& cfg);
    ~App();

    void update();

    void connect(const std::string& server_url);
    void disconnect();
    void toggle_mute();
    void toggle_deafen();
    void set_volume(float vol);

    void start_sharing(const CaptureTarget& target, StreamQuality quality, int fps);
    void stop_sharing();
    bool sharing() const { return sharing_; }

    void update_preview(const CaptureTarget& target);
    void clear_preview();

    AppState state() const { return state_; }
    bool muted() const { return audio_.muted(); }
    bool deafened() const { return audio_.deafened(); }
    float volume() const { return audio_.output_volume(); }
    float input_level() const { return audio_.input_level(); }
    float output_level() const { return audio_.output_level(); }
    std::string local_id() const { return transport_.local_id(); }
    const Config& config() const noexcept { return config_; }

    VideoRenderer& video_renderer() { return video_renderer_; }
    StreamStats stream_stats(const std::string& peer_id) const;

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
        std::chrono::steady_clock::time_point last_frame;
        int measured_kbps = 0;

        uint16_t reassembly_frame_id = 0;
        uint16_t reassembly_total = 0;
        uint16_t reassembly_got = 0;
        std::vector<uint8_t> reassembly_buf;
        uint32_t reassembly_w = 0, reassembly_h = 0, reassembly_kbps = 0;
    };
    mutable std::mutex video_mutex_;
    std::unordered_map<std::string, std::unique_ptr<PeerVideoState>> peer_video_;
    std::vector<std::string> pending_removals_;

    uint16_t send_frame_id_ = 0;
    std::vector<uint8_t> send_buf_;
};
