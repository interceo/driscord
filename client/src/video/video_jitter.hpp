#pragma once

#include "utils/jitter.hpp"
#include "utils/time.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

class VideoJitter {
public:
    struct Frame {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
    };

    explicit VideoJitter(int buffer_ms) : buf_(buffer_ms) {}

    // frame_duration_us: real frame interval from the sender (1_000_000 / fps).
    void push(
        std::vector<uint8_t> rgba,
        int w,
        int h,
        uint32_t frame_duration_us,
        utils::WallTimestamp sender_ts = {}
    ) {
        if (frame_duration_us > 0) {
            buf_.set_packet_duration(std::chrono::microseconds(frame_duration_us));
        }
        buf_.push(seq_++, Frame{std::move(rgba), w, h}, sender_ts);
    }

    // Returns a new frame, or nullptr if nothing new is ready.
    const Frame* pop() {
        auto pkt = buf_.pop();
        if (!pkt) {
            return nullptr;
        }
        current_ = std::move(pkt->data);
        return &*current_;
    }

    size_t buffered_ms() const { return buf_.buffered_ms(); }
    size_t queue_size() const { return buf_.queue_size(); }

    void reset() {
        buf_.reset();
        seq_ = 0;
        current_.reset();
    }

    using Stats = JitterBuffer<Frame>::Stats;
    Stats stats() const { return buf_.stats(); }

private:
    JitterBuffer<Frame> buf_;
    uint64_t seq_ = 0;
    std::optional<Frame> current_;
};
