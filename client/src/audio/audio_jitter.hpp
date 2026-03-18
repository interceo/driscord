#pragma once

#include "utils/jitter.hpp"

#include <cstdint>
#include <vector>

struct PcmFrame {
    std::vector<float> samples;
};

class AudioJitter {
public:
    explicit AudioJitter(size_t target_delay_ms)
        : buf_(static_cast<int>(target_delay_ms)) {}

    void push(std::vector<float> samples, uint64_t seq) {
        if (samples.empty()) {
            return;
        }
        buf_.push(seq, PcmFrame{std::move(samples)});
    }

    std::vector<float> pop() {
        auto pkt = buf_.pop();
        if (!pkt || pkt->data.samples.empty()) {
            return {};
        }
        return std::move(pkt->data.samples);
    }

    // Discard all queued packets older than max_delay_ms.
    size_t evict_old(int max_delay_ms) { return buf_.evict_old(max_delay_ms); }

    size_t queue_size() const { return buf_.queue_size(); }
    void reset() { buf_.reset(); }

    using Stats = JitterBuffer<PcmFrame>::Stats;
    Stats stats() const { return buf_.stats(); }

private:
    JitterBuffer<PcmFrame> buf_;
};
