#pragma once

#include "capture/screen_capture.hpp"
#include "utils/jitter.hpp"
#include "utils/protocol.hpp"
#include "utils/video_codec.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
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
        std::string peer_id;
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

    void push_video_packet(
        const std::string& peer_id,
        const utils::vector_view<const uint8_t> data
    );

    const Frame* update();

    void set_keyframe_callback(std::function<void()> fn);

    std::string active_peer() const;
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
    // Per-peer state — each peer has its own H.264 decoder and jitter buffer.
    // Created lazily on first packet, destroyed on reset().
    struct PeerDecoder {
        VideoDecoder decoder;
        VideoJitter jitter;
        int decode_failures = 0;
        utils::Timestamp last_keyframe_req{};

        explicit PeerDecoder(utils::Duration buf_delay) : jitter(buf_delay) {}
    };

    std::function<void()> on_keyframe_needed_;

    utils::Duration buffer_delay_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<PeerDecoder>> peer_decoders_;
    std::string current_peer_;
    utils::Timestamp last_packet_{};

    // Bitrate measurement — producer thread only.
    std::atomic<int> measured_kbps_{0};
    size_t bytes_since_calc_ = 0;
    utils::Timestamp last_calc_{};

    VideoJitter::Ptr current_frame_;
};
