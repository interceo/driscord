#pragma once

#include "capture/screen_capture.hpp"
#include "utils/protocol.hpp"
#include "utils/video_codec.hpp"
#include "video_jitter.hpp"

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

    bool start(int fps, int base_bitrate_kbps, int gop_size, SendCb on_video);
    void stop();

    void push_frame(ScreenCapture::Frame&& frame);

    bool sharing() const { return sharing_; }
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

    int fps_               = 0;
    int base_bitrate_kbps_ = 0;
    int gop_size           = 0;

    std::vector<uint8_t> frame_buf_;

    SendCb on_video_;
};

class VideoReceiver {
public:
    VideoReceiver(int buffer_ms, int max_sync_gap_ms);
    ~VideoReceiver();

    VideoReceiver(const VideoReceiver&)            = delete;
    VideoReceiver& operator=(const VideoReceiver&) = delete;

    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len);

    const VideoJitter::Frame* update();
    const VideoJitter::Frame* current_frame() const;

    void set_keyframe_callback(std::function<void()> fn);

    std::string active_peer() const;
    bool active() const;
    int measured_kbps() const { return measured_kbps_.load(std::memory_order_relaxed); }

    VideoJitter::Stats video_stats() const { return video_.stats(); }

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
};
