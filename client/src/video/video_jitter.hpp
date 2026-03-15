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

    explicit VideoJitter(int buffer_ms) : buf_(buffer_ms, true /* pace_by_sender_ts */) {}

    void push(std::vector<uint8_t> rgba, int w, int h, utils::WallTimestamp sender_ts) {
        buf_.push(seq_++, Frame{std::move(rgba), w, h}, sender_ts);
    }

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
