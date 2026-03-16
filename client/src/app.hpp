#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/audio_mixer.hpp"
#include "audio/audio_receiver.hpp"
#include "audio/audio_sender.hpp"
#include "audio/capture/system_audio_capture.hpp"
#include "config.hpp"
#include "video/capture/screen_capture.hpp"
#include "video/screen_session.hpp"
#include "video/video_sender.hpp"
#include "video_renderer.hpp"
#include "voice_transport.hpp"

inline constexpr const char* kPreviewPeerId = "__preview__";

enum class AppState {
    Disconnected,
    Connecting,
    Connected,
};

#include "stream_defs.hpp"

struct StreamStats {
    int width = 0;
    int height = 0;
    int measured_kbps = 0;

    struct JitterStats {
        int video_queue = 0;
        int video_buf_ms = 0;
        uint64_t video_drops = 0;
        uint64_t video_misses = 0;

        int audio_queue = 0;
        int audio_buf_ms = 0;
        uint64_t audio_drops = 0;
        uint64_t audio_misses = 0;
    } jitter;
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

    void set_stream_volume(float vol) { screen_session_.audio_receiver()->set_volume(vol); }
    float stream_volume() const { return screen_session_.audio_receiver()->volume(); }

    void join_stream();
    void leave_stream();
    bool watching_stream() const { return watching_stream_.load(); }

    void start_sharing(const CaptureTarget& target, StreamQuality quality, int fps, bool share_audio = false);
    void stop_sharing();
    bool sharing() const { return sender_.sharing(); }
    bool sharing_audio() const { return sender_.sharing_audio(); }
    static bool system_audio_available() { return SystemAudioCapture::available(); }

    void update_preview(const CaptureTarget& target);
    void clear_preview();

    AppState state() const { return state_; }
    bool muted() const { return audio_sender_.muted(); }
    bool deafened() const { return audio_mixer_.deafened(); }
    float volume() const { return audio_mixer_.output_volume(); }
    float input_level() const { return audio_sender_.input_level(); }
    float output_level() const { return audio_mixer_.output_level(); }
    std::string local_id() const { return transport_.local_id(); }
    const Config& config() const noexcept { return config_; }

    VideoRenderer& video_renderer() { return video_renderer_; }
    StreamStats stream_stats(const std::string& peer_id) const;

    struct PeerView {
        std::string id;
        bool connected;
    };
    std::vector<PeerView> peers() const;
    std::vector<std::string> streaming_peers() const;

private:
    AudioReceiver* get_or_create_voice(const std::string& peer_id);

    Config config_;
    AppState state_ = AppState::Disconnected;

    AudioSender audio_sender_;
    AudioMixer audio_mixer_;
    ScreenSession screen_session_{config_.screen_buffer_ms, config_.max_sync_gap_ms};
    VideoSender sender_;
    VoiceTransport transport_;
    VideoRenderer video_renderer_;

    std::atomic<bool> watching_stream_{false};

    std::string last_rendered_peer_;
    int last_frame_w_ = 0;
    int last_frame_h_ = 0;

    mutable std::mutex voice_mutex_;
    std::unordered_map<std::string, std::unique_ptr<AudioReceiver>> voice_receivers_;

    mutable std::mutex peer_vol_mutex_;
    std::unordered_map<std::string, float> peer_volumes_;

    mutable std::mutex streaming_mutex_;
    std::set<std::string> video_active_peers_;
};
