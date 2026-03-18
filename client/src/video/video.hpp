#pragma once

#include "capture/screen_capture.hpp"
#include "utils/jitter.hpp"
#include "utils/protocol.hpp"
#include "utils/video_codec.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <semaphore>
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

class VideoReceiver {
public:
    struct Frame {
        std::vector<uint8_t> rgba;
        int width  = 0;
        int height = 0;
        utils::WallTimestamp sender_ts{};
        utils::Duration frame_duration{};

        bool empty() const noexcept { return rgba.empty(); }
    };

    using VideoJitter = utils::Jitter<Frame>;
    using Stats       = VideoJitter::Stats;

    VideoReceiver(int buffer_ms, int max_sync_gap_ms);
    ~VideoReceiver();

    VideoReceiver(const VideoReceiver&)            = delete;
    VideoReceiver& operator=(const VideoReceiver&) = delete;

    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len);

    const Frame* update();

    void set_keyframe_callback(std::function<void()> fn);

    std::string active_peer() const;
    bool active() const;
    int measured_kbps() const { return measured_kbps_.load(std::memory_order_relaxed); }

    Stats video_stats() const { return video_.stats(); }

    size_t evict_old(utils::Duration max_delay) { return video_.evict_old(max_delay); }
    size_t evict_before_sender_ts(utils::WallTimestamp cutoff) { return video_.evict_before_sender_ts(cutoff); }
    std::optional<utils::WallTimestamp> front_effective_ts() const { return video_.front_effective_ts(); }
    utils::Duration front_frame_duration() const {
        return video_.with_front([](const Frame& f) { return f.frame_duration; }).value_or(utils::Duration{});
    }
    bool primed() const { return video_.primed(); }
    int64_t front_age_ms() const { return video_.front_age_ms(); }

    void reset();

private:
    void decode_loop();

    struct FrameSlot {
        std::string peer_id;
        std::vector<uint8_t> encoded;
        protocol::VideoHeader vh;
    };

    static constexpr size_t kQueueCapacity = 4;
    std::array<FrameSlot, kQueueCapacity> ring_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::counting_semaphore<kQueueCapacity> sem_{0};

    std::thread decode_thread_;
    std::atomic<bool> decode_running_{false};

    VideoDecoder decoder_;
    int decode_failures_ = 0;
    utils::Timestamp last_keyframe_req_{};

    std::function<void()> on_keyframe_needed_;

    VideoJitter video_;

    mutable std::mutex mutex_;
    std::string current_peer_;
    utils::Timestamp last_packet_{};

    // Bitrate measurement — producer thread only.
    std::atomic<int> measured_kbps_{0};
    size_t bytes_since_calc_ = 0;
    utils::Timestamp last_calc_{};

    std::optional<Frame> current_frame_;
};
