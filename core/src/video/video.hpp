#pragma once

#include "capture/screen_capture.hpp"
#include "sync/media_clock.hpp"
#include "utils/metrics.hpp"
#include "utils/mono_clock.hpp"
#include "utils/reorder_buffer.hpp"
#include "video_codec.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

class VideoSender {
public:
    using SendCb = std::function<void(const uint8_t* data, size_t len)>;

    VideoSender();
    ~VideoSender();

    VideoSender(const VideoSender&) = delete;
    VideoSender& operator=(const VideoSender&) = delete;

    bool start(const size_t fps, const size_t base_bitrate_kbps, SendCb on_video);
    void stop();

    // Takes the frame's contents by swap, leaving its previous buffer with the
    // caller to reuse.
    void push_frame(ScreenCapture::Frame& frame);

    bool sharing() const noexcept { return sharing_; }
    void force_keyframe() { video_encoder_.force_keyframe(); }
    int measured_kbps() const { return video_encoder_.measured_kbps(); }
    int width() const { return video_encoder_.width(); }
    int height() const { return video_encoder_.height(); }

private:
    void encode_loop();

    std::atomic<bool> sharing_ { false };

    VideoEncoder video_encoder_;

    std::thread encode_thread_;
    std::atomic<bool> encode_running_ { false };
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    ScreenCapture::Frame pending_frame_;
    bool frame_ready_ = false;

    size_t fps_ = 0;
    size_t base_bitrate_kbps_ = 0;

    std::vector<uint8_t> frame_buf_;

    SendCb on_video_;
};

// Single-peer video receiver: one H.264 decoder, one reordering buffer.
// Per-peer lifecycle (creation, routing) is managed by ScreenReceiver.
//
// Frames are shown when the shared MediaClock says they are due, which is the
// same rule the peer's audio plays by. That is the whole of the A/V
// synchronisation: there is no drift to detect and no stream to fast-forward,
// because neither side was ever free to run on its own schedule.
class VideoReceiver {
public:
    struct Frame {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        std::string peer_id;
        int64_t sender_ts_us = 0;

        bool empty() const noexcept { return rgba.empty(); }
    };

    struct Stats {
        size_t queue_size = 0;
        uint64_t drop_count = 0;
        uint64_t late_count = 0; // frames superseded before they were shown
        uint64_t packets_received = 0;
        uint64_t decode_failures = 0;
        uint64_t keyframe_requests = 0;
        int measured_kbps = 0;
        int64_t target_delay_ms = 0;
        int64_t p50_delay_ms = -1;
        int64_t p95_delay_ms = -1;
        int64_t p99_delay_ms = -1;
        uint64_t delay_samples = 0;
        int64_t last_shown_ts_us = 0;
    };

    VideoReceiver(std::string peer_id,
        std::shared_ptr<avsync::MediaClock> clock,
        const utils::TimeSource& time = utils::system_time_source());
    ~VideoReceiver();

    VideoReceiver(const VideoReceiver&) = delete;
    VideoReceiver& operator=(const VideoReceiver&) = delete;

    void push_video_packet(std::span<const uint8_t> data,
        uint64_t frame_id);

    // Shows the newest frame that is due, discarding any older ones still
    // queued behind it — for a display, only the latest picture matters.
    void update(const std::function<void(const Frame&)>& on_frame);

    void set_keyframe_callback(std::function<void()> fn);

    bool active() const;
    int measured_kbps() const
    {
        return measured_kbps_.load(std::memory_order_relaxed);
    }

    Stats video_stats() const;

    void reset();

private:
    // ~1 s of reordering room at 60 fps. Video frames are large, so this is
    // sized for reordering, not for buffering delay.
    static constexpr size_t kBufferCapacity = 64;
    static constexpr size_t kMaxSpareBuffers = 4;

    // Frame buffers are recycled rather than reallocated; at 1080p60 that is
    // 8 MB of allocation and free per frame otherwise.
    std::vector<uint8_t> take_spare();
    void recycle(std::vector<uint8_t>&& buf);
    void recycle_locked(std::vector<uint8_t>&& buf); // caller holds mutex_

    std::string peer_id_;
    std::shared_ptr<avsync::MediaClock> clock_;
    const utils::TimeSource* time_;
    std::function<void()> on_keyframe_needed_;

    VideoDecoder decoder_;
    std::optional<VideoCodec> decoder_codec_; // nullopt = not yet initialised

    mutable std::mutex mutex_;
    utils::ReorderBuffer<Frame, kBufferCapacity> buffer_;
    Frame current_frame_;
    std::vector<std::vector<uint8_t>> spare_;

    int64_t last_packet_us_ = -1; // -1 until the first packet arrives
    int decode_failures_ = 0;
    int64_t last_keyframe_req_us_ = -1;

    // Bitrate measurement — producer thread only.
    std::atomic<int> measured_kbps_ { 0 };
    size_t bytes_since_calc_ = 0;
    int64_t last_calc_us_ = 0;

    utils::Counter packets_received_;
    utils::Counter drop_count_;
    utils::Counter late_count_;
    utils::Counter total_decode_failures_;
    utils::Counter keyframe_requests_;
};
