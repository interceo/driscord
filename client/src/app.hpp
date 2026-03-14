#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/audio_engine.hpp"
#include "audio/system_audio_capture.hpp"
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
    void set_peer_volume(const std::string& peer_id, float vol);
    float peer_volume(const std::string& peer_id) const;

    void start_sharing(const CaptureTarget& target, StreamQuality quality, int fps, bool share_audio = false);
    void stop_sharing();
    bool sharing() const { return sharing_; }
    bool sharing_audio() const { return sharing_audio_; }
    static bool system_audio_available() { return SystemAudioCapture::available(); }

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
    bool sharing_audio_ = false;
    std::atomic<bool> encoding_{false};

    AudioEngine audio_{config_.voice_jitter_ms, config_.screen_buffer_ms};
    VoiceTransport transport_;
    VideoRenderer video_renderer_;

    std::unique_ptr<ScreenCapture> screen_capture_;
    std::unique_ptr<SystemAudioCapture> system_audio_capture_;
    VideoEncoder video_encoder_;

    struct TimedFrame {
        std::vector<uint8_t> rgba;
        int width, height;
        uint32_t sender_ts;
    };

    static constexpr size_t kMaxFrameQueue = 10;

    struct PeerVideoState {
        VideoDecoder decoder;
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        bool dirty = false;
        std::chrono::steady_clock::time_point last_frame;
        int measured_kbps = 0;

        std::vector<uint8_t> pending_decode;
        uint32_t pending_kbps = 0;
        uint32_t pending_sender_ts = 0;
        bool has_pending_decode = false;
        int decode_failures = 0;

        std::deque<TimedFrame> frame_queue;

        uint16_t reassembly_frame_id = 0;
        uint16_t reassembly_total = 0;
        uint16_t reassembly_got = 0;
        std::vector<uint8_t> reassembly_buf;
    };
    mutable std::mutex video_mutex_;
    std::unordered_map<std::string, std::shared_ptr<PeerVideoState>> peer_video_;
    std::vector<std::string> pending_removals_;

    mutable std::mutex peer_vol_mutex_;
    std::unordered_map<std::string, float> peer_volumes_;

    uint16_t send_frame_id_ = 0;
    std::vector<uint8_t> frame_buf_;
    std::vector<uint8_t> send_buf_;
};
