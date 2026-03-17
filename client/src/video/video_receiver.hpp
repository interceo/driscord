#pragma once

#include "utils/protocol.hpp"
#include "utils/video_codec.hpp"
#include "video_jitter.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class VideoReceiver {
public:
    VideoReceiver(int buffer_ms, int max_sync_gap_ms);
    ~VideoReceiver();

    VideoReceiver(const VideoReceiver&) = delete;
    VideoReceiver& operator=(const VideoReceiver&) = delete;

    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len);

    // Advance the jitter buffer and return the next frame to display, or nullptr.
    const VideoJitter::Frame* update();

    // Return the last frame returned by update() without advancing.
    // Valid until the next update() call that returns non-null.
    const VideoJitter::Frame* current_frame() const;

    void set_keyframe_callback(std::function<void()> fn);

    std::string active_peer() const;
    bool active() const;
    int measured_kbps() const { return measured_kbps_.load(std::memory_order_relaxed); }

    VideoJitter::Stats video_stats() const { return video_.stats(); }

    void reset();

private:
    struct DecodeJob {
        std::vector<uint8_t> encoded;
        protocol::VideoHeader vh;
    };

    void decode_loop();

    VideoJitter video_;

    mutable std::mutex mutex_;
    std::string current_peer_;
    utils::Timestamp last_packet_{};

    VideoDecoder decoder_;
    int decode_failures_ = 0;
    utils::Timestamp last_keyframe_req_{};
    std::atomic<int> measured_kbps_{0};

    std::function<void()> on_keyframe_needed_;

    size_t bytes_since_calc_ = 0;
    utils::Timestamp last_calc_{};

    uint64_t push_count_ = 0;
    uint64_t pop_count_ = 0;

    std::deque<DecodeJob> decode_queue_;
    std::mutex decode_queue_mutex_;
    std::condition_variable decode_cv_;
    std::thread decode_thread_;
    std::atomic<bool> decode_running_{false};

    static constexpr size_t kMaxDecodeQueue = 4;
};
