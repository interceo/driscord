#pragma once

#include "audio/audio.hpp"
#include "audio/capture/system_audio_capture.hpp"
#include "opus_codec.hpp"
#include "video/capture/screen_capture.hpp"
#include "video/video.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ScreenSender {
public:
    using SendCb = std::function<void(const uint8_t* data, size_t len)>;

    ScreenSender() = default;
    ~ScreenSender();

    ScreenSender(const ScreenSender&) = delete;
    ScreenSender& operator=(const ScreenSender&) = delete;

    utils::Expected<void, VideoError> start_sharing(const ScreenCaptureTarget& target,
        const size_t max_w,
        const size_t max_h,
        const size_t fps,
        bool share_audio,
        SendCb on_video,
        SendCb on_screen_audio);

    void stop_sharing();

    bool sharing() const { return video_sender_.sharing(); }
    bool sharing_audio() const
    {
        return system_audio_capture_ && system_audio_capture_->running();
    }

    void force_keyframe() { video_sender_.force_keyframe(); }
    int sender_kbps() const { return video_sender_.measured_kbps(); }

    void set_system_audio_bitrate(int kbps) { system_audio_bitrate_kbps_ = kbps; }

private:
    int system_audio_bitrate_kbps_ = 128;
    void on_audio_captured_(const float* samples, size_t frames, int channels);

    VideoSender video_sender_;
    std::unique_ptr<ScreenCapture> screen_capture_;
    std::unique_ptr<SystemAudioCapture> system_audio_capture_;
    std::unique_ptr<OpusEncode> screen_audio_encoder_;
    SendCb on_screen_audio_;
    std::vector<float> screen_audio_buf_;
    std::vector<uint8_t> screen_audio_encode_buf_;
    size_t screen_audio_pos_ = 0;
    uint64_t screen_audio_seq_ = 0;
};

class ScreenReceiver {
public:
    ScreenReceiver(int buffer_ms, int max_sync_gap_ms, int audio_jitter_ms);
    ~ScreenReceiver() = default;

    ScreenReceiver(const ScreenReceiver&) = delete;
    ScreenReceiver& operator=(const ScreenReceiver&) = delete;

    // Video peer lifecycle — must be called before push_video_packet for that
    // peer.
    void add_video_peer(const std::string& peer_id);
    void remove_video_peer(const std::string& peer_id);

    void push_video_packet(const std::string& peer_id,
        const utils::vector_view<const uint8_t> data,
        uint64_t frame_id);
    void push_audio_packet(const std::string& peer_id,
        const utils::vector_view<const uint8_t> data);

    // Per-peer audio receiver lifecycle. Must be called before push_audio_packet
    // for that peer.
    void add_audio_peer(const std::string& peer_id);
    void remove_audio_peer(const std::string& peer_id);
    std::shared_ptr<AudioReceiver> audio_receiver(const std::string& peer_id);
    std::shared_ptr<const AudioReceiver> audio_receiver(
        const std::string& peer_id) const;

    void update(std::function<void(const VideoReceiver::Frame&)> on_frame);

    void set_keyframe_callback(std::function<void()> fn);

    std::string active_peer() const;
    bool active() const;
    std::unordered_set<std::string> active_peers() const;
    int measured_kbps() const;

    VideoReceiver::Stats video_stats() const;
    AudioReceiver::Stats audio_stats()
        const; // aggregated across all audio peers

    // Hard timeout eviction.
    void evict_old(utils::Duration max_delay);
    void evict_old_video(utils::Duration max_delay);

    // A/V sync — sender-timestamp-based.
    bool video_primed() const;
    bool audio_primed() const;
    std::optional<utils::WallTimestamp> video_front_effective_ts() const;
    std::optional<utils::WallTimestamp> audio_front_effective_ts() const;
    utils::Duration video_frame_duration() const;
    size_t evict_video_before(utils::WallTimestamp cutoff);
    size_t evict_audio_before(utils::WallTimestamp cutoff);

    // Clock-skew estimators: median OWD+skew per stream.
    // Returns -1 if not enough samples yet.
    int64_t video_median_ow_delay_ms() const;
    int64_t audio_median_ow_delay_ms() const;

    int64_t video_front_age_ms() const;
    int64_t audio_front_age_ms() const;

    void reset();
    void reset_audio();

private:
    // Returns the VideoReceiver for the current peer; must be called with
    // video_mutex_ held.
    std::shared_ptr<VideoReceiver> current_video_recv_locked() const;

    int video_buffer_ms_;
    std::function<void()> keyframe_cb_;

    mutable std::mutex video_mutex_;
    std::unordered_map<std::string, std::shared_ptr<VideoReceiver>>
        video_receivers_;
    std::string current_video_peer_;

    mutable std::mutex audio_mutex_;
    std::unordered_map<std::string, std::shared_ptr<AudioReceiver>>
        audio_receivers_;
    int audio_jitter_ms_;
};
