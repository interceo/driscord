#pragma once

#include "utils/jitter.hpp"

#include <cstdint>
#include <optional>
#include <vector>

class VideoJitter {
public:
    struct Frame {
        std::vector<uint8_t> rgba;
        int width  = 0;
        int height = 0;
    };

    explicit VideoJitter(int target_delay_ms)
        : buf_(target_delay_ms) {}

    void push(std::vector<uint8_t> rgba, int w, int h) {
        if (rgba.empty()) {
            return;
        }
        buf_.push(seq_++, Frame{std::move(rgba), w, h});
    }

    Frame pop() {
        auto pkt = buf_.pop();
        if (!pkt || pkt->data.rgba.empty()) {
            return Frame{};
        }
        return std::move(pkt->data);
    }

    size_t queue_size() const { return buf_.queue_size(); }

    // Discard frames older than max_delay_ms (e.g. for A/V sync).
    size_t evict_old(int max_delay_ms) { return buf_.evict_old(max_delay_ms); }

    void reset() {
        buf_.reset();
        seq_ = 0;
    }

    using Stats = JitterBuffer<Frame>::Stats;
    Stats stats() const { return buf_.stats(); }

private:
    JitterBuffer<Frame> buf_;
    uint64_t seq_ = 0;
};
