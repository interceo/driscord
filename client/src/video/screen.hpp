#pragma once

#include "audio/audio.hpp"
#include "audio/capture/system_audio_capture.hpp"
#include "utils/opus_codec.hpp"
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

    ScreenSender(const ScreenSender&)            = delete;
    ScreenSender& operator=(const ScreenSender&) = delete;

    bool start_sharing(
        const CaptureTarget& target,
        const size_t max_w,
        const size_t max_h,
        const size_t fps,
        const size_t bitrate_kbps,
        bool share_audio,
        SendCb on_video,
        SendCb on_screen_audio
    );

    void stop_sharing();

    bool sharing() const { return video_sender_.sharing(); }
    bool sharing_audio() const { return system_audio_capture_ && system_audio_capture_->running(); }

    void force_keyframe() { video_sender_.force_keyframe(); }
    int sender_kbps() const { return video_sender_.measured_kbps(); }

private:
    void on_audio_captured_(const float* samples, size_t frames, int channels);

    VideoSender video_sender_;
    std::unique_ptr<ScreenCapture> screen_capture_;
    std::unique_ptr<SystemAudioCapture> system_audio_capture_;
    std::unique_ptr<OpusEncode> screen_audio_encoder_;
    SendCb on_screen_audio_;
    std::vector<float> screen_audio_buf_;
    std::vector<uint8_t> screen_audio_encode_buf_;
    size_t screen_audio_pos_   = 0;
    uint64_t screen_audio_seq_ = 0;
};

class ScreenReceiver {
public:
    ScreenReceiver(int buffer_ms, int max_sync_gap_ms);
    ~ScreenReceiver() = default;

    ScreenReceiver(const ScreenReceiver&)            = delete;
    ScreenReceiver& operator=(const ScreenReceiver&) = delete;

    void push_video_packet(
        const std::string& peer_id,
        const utils::vector_view<const uint8_t> data
    );
    void push_audio_packet(const std::string& peer_id, const utils::vector_view<const uint8_t> data);

    // Per-peer audio receiver lifecycle. Must be called before push_audio_packet for that peer.
    void add_audio_peer(const std::string& peer_id);
    void remove_audio_peer(const std::string& peer_id);
    std::shared_ptr<AudioReceiver> audio_receiver(const std::string& peer_id);
    std::shared_ptr<const AudioReceiver> audio_receiver(const std::string& peer_id) const;

    void update(std::function<void(const VideoReceiver::Frame&)> on_frame);

    void set_keyframe_callback(std::function<void()> fn);

    std::string active_peer() const;
    bool active() const;
    std::unordered_set<std::string> active_peers() const;
    int measured_kbps() const;

    VideoReceiver::Stats video_stats() const;
    AudioReceiver::Stats audio_stats() const; // aggregated across all audio peers

    // Hard timeout eviction.
    void evict_old(utils::Duration max_delay);
    void evict_old_video(utils::Duration max_delay) { video_recv_.evict_old(max_delay); }

    // A/V sync — sender-timestamp-based.
    bool video_primed() const { return video_recv_.primed(); }
    bool audio_primed() const;
    std::optional<utils::WallTimestamp> video_front_effective_ts() const {
        return video_recv_.front_effective_ts();
    }
    std::optional<utils::WallTimestamp> audio_front_effective_ts() const;
    utils::Duration video_frame_duration() const { return video_recv_.front_frame_duration(); }
    size_t evict_video_before(utils::WallTimestamp cutoff) {
        return video_recv_.evict_before_sender_ts(cutoff);
    }

    int64_t video_front_age_ms() const { return video_recv_.front_age_ms(); }
    int64_t audio_front_age_ms() const;

    void reset();
    void reset_audio();

private:
    VideoReceiver video_recv_;

    mutable std::mutex audio_mutex_;
    std::unordered_map<std::string, std::shared_ptr<AudioReceiver>> audio_receivers_;
    int audio_jitter_ms_;
};
