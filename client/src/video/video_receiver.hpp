#pragma once

#include "video_jitter.hpp"
#include "utils/protocol.hpp"
#include "utils/video_codec.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class VideoReceiver {
public:
    VideoReceiver(int buffer_ms, int max_sync_gap_ms);
    ~VideoReceiver();

    VideoReceiver(const VideoReceiver&) = delete;
    VideoReceiver& operator=(const VideoReceiver&) = delete;

    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len);

    const VideoJitter::Frame* update();

    void set_keyframe_callback(std::function<void()> fn);

    std::string active_peer() const;
    bool active() const;
    int measured_kbps() const { return measured_kbps_; }

    VideoJitter::Stats video_stats() const { return video_.stats(); }

    void reset();

private:
    struct ChunkReassembler {
        uint16_t frame_id = 0;
        uint16_t total = 0;
        uint16_t got = 0;
        std::vector<uint8_t> buf;
    };

    struct PendingFrame {
        std::vector<uint8_t> data;
        uint32_t kbps = 0;
        utils::WallTimestamp ts{};
        uint32_t duration_us = 0;
    };

    VideoJitter video_;

    mutable std::mutex mutex_;
    std::string current_peer_;
    utils::Timestamp last_packet_{};
    ChunkReassembler reassembler_;
    std::optional<PendingFrame> pending_;

    VideoDecoder decoder_;
    int decode_failures_ = 0;
    utils::Timestamp last_keyframe_req_{};
    int measured_kbps_ = 0;

    std::function<void()> on_keyframe_needed_;
};
