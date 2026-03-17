#pragma once

#include "utils/jitter.hpp"
#include "utils/time.hpp"

#include <cstdint>
#include <optional>
#include <vector>

class VideoJitter {
public:
    struct Frame {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        utils::WallTimestamp sender_ts{};
    };

    static constexpr int    kDefaultMaxExcessMs  = 200;
    static constexpr size_t kDefaultMaxQueueSize = 8;

    explicit VideoJitter(int buffer_ms,
                         int    max_excess_ms  = kDefaultMaxExcessMs,
                         size_t max_queue_size = kDefaultMaxQueueSize)
        : buf_(buffer_ms, /*pace_by_sender_ts=*/true, max_excess_ms, max_queue_size) {}

    void push(std::vector<uint8_t> rgba, int w, int h, utils::WallTimestamp sender_ts) {
        buf_.push(seq_++, Frame{std::move(rgba), w, h, sender_ts}, sender_ts);
    }

    const Frame* pop() {
        auto pkt = buf_.pop();
        if (!pkt) {
            return nullptr;
        }
        current_ = std::move(pkt->data);
        return &*current_;
    }

    // Returns the last popped frame without advancing. Valid until the next pop().
    const Frame* current() const noexcept {
        return current_ ? &*current_ : nullptr;
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
