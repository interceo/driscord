#pragma once

#include "capture/screen_capture.hpp"
#include "utils/jitter.hpp"
#include "utils/video_codec.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class VideoSender {
public:
    using SendCb = std::function<void(const uint8_t* data, size_t len)>;

    VideoSender();
    ~VideoSender();

    VideoSender(const VideoSender&)            = delete;
    VideoSender& operator=(const VideoSender&) = delete;

    bool start(const size_t fps, const size_t base_bitrate_kbps, SendCb on_video);
    void stop();

    void push_frame(ScreenCapture::Frame&& frame);

    bool sharing() const noexcept { return sharing_; }
    void force_keyframe() { video_encoder_.force_keyframe(); }
    int measured_kbps() const { return video_encoder_.measured_kbps(); }
    int width() const { return video_encoder_.width(); }
    int height() const { return video_encoder_.height(); }

private:
    void encode_loop();

    std::atomic<bool> sharing_{false};

    VideoEncoder video_encoder_;

    std::thread encode_thread_;
    std::atomic<bool> encode_running_{false};
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    ScreenCapture::Frame pending_frame_;
    bool frame_ready_ = false;

    size_t fps_               = 0;
    size_t base_bitrate_kbps_ = 0;

    std::vector<uint8_t> frame_buf_;

    SendCb on_video_;
};

// Single-peer video receiver: one H.264 decoder, one jitter buffer.
// Per-peer lifecycle (creation, routing) is managed by ScreenReceiver.
class VideoReceiver {
public:
    struct Frame {
        std::vector<uint8_t> rgba;
        int width  = 0;
        int height = 0;
        std::string peer_id;
        utils::WallTimestamp sender_ts{};
        utils::Duration frame_duration{};

        bool empty() const noexcept { return rgba.empty(); }
    };

    using VideoJitter = utils::Jitter<Frame>;
    using Stats       = VideoJitter::Stats;

    VideoReceiver(std::string peer_id, int buffer_ms);
    ~VideoReceiver();

    VideoReceiver(const VideoReceiver&)            = delete;
    VideoReceiver& operator=(const VideoReceiver&) = delete;

    void push_video_packet(utils::vector_view<const uint8_t> data);

    // Drains jitter, calls on_frame if a current frame is available.
    void update(std::function<void(const Frame&)> on_frame);

    void set_keyframe_callback(std::function<void()> fn);

    bool active() const;
    int measured_kbps() const { return measured_kbps_.load(std::memory_order_relaxed); }

    Stats video_stats() const;

    size_t evict_old(utils::Duration max_delay);
    size_t evict_before_sender_ts(utils::WallTimestamp cutoff);
    std::optional<utils::WallTimestamp> front_effective_ts() const;
    utils::Duration front_frame_duration() const;
    bool primed() const;
    int64_t front_age_ms() const;

    void reset();

private:
    std::string peer_id_;
    std::function<void()> on_keyframe_needed_;
    utils::Duration buffer_delay_;

    VideoDecoder decoder_;
    VideoJitter jitter_;
    VideoJitter::Ptr current_frame_;

    mutable std::mutex mutex_;
    utils::Timestamp last_packet_{};
    int decode_failures_ = 0;
    utils::Timestamp last_keyframe_req_{};

    // Bitrate measurement — producer thread only.
    std::atomic<int> measured_kbps_{0};
    size_t bytes_since_calc_ = 0;
    utils::Timestamp last_calc_{};
};
