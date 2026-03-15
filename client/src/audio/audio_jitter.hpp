#pragma once

#include "utils/jitter.hpp"

#include <vector>

inline constexpr uint32_t kDefaultJitterMs = 80;

struct PcmFrame {
    std::vector<float> samples;
};

// Audio jitter buffer.
//
// push() — network/decode thread.
// pop()  — audio callback thread.
class AudioJitter {
public:
    explicit AudioJitter(size_t target_delay_ms = kDefaultJitterMs) : buf_(static_cast<int>(target_delay_ms)) {}

    void push(std::vector<float> samples, uint64_t seq, utils::WallTimestamp sender_ts) {
        if (samples.empty()) {
            return;
        }
        buf_.push(seq, PcmFrame{std::move(samples)}, sender_ts);
    }

    std::vector<float> pop() {
        auto pkt = buf_.pop();
        if (!pkt || pkt->data.samples.empty()) {
            return {};
        }
        return std::move(pkt->data.samples);
    }

    size_t buffered_ms() const { return buf_.buffered_ms(); }
    void reset() { buf_.reset(); }

    using Stats = JitterBuffer<PcmFrame>::Stats;
    Stats stats() const { return buf_.stats(); }

private:
    JitterBuffer<PcmFrame> buf_;
};
