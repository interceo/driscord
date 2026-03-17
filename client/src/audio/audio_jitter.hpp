#pragma once

#include "utils/jitter.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

struct PcmFrame {
    std::vector<float> samples;
};

class AudioJitter {
public:
    explicit AudioJitter(size_t target_delay_ms)
        : buf_(static_cast<int>(target_delay_ms), false) {}

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
        if (pkt->sender_ts.time_since_epoch().count() != 0) {
            last_ts_ms_.store(utils::WallToMs(pkt->sender_ts), std::memory_order_relaxed);
        }
        return std::move(pkt->data.samples);
    }

    // Wall-clock timestamp (ms since epoch) of the last successfully popped frame.
    // Zero if nothing has been popped yet. Updated from the audio callback thread.
    uint64_t last_ts_ms() const noexcept { return last_ts_ms_.load(std::memory_order_relaxed); }

    // Discard all queued packets older than ts_ms. Called from the render thread.
    size_t drain_before(uint64_t ts_ms) { return buf_.drain_before(ts_ms); }

    size_t queue_size() const { return buf_.queue_size(); }
    size_t buffered_ms() const { return buf_.buffered_ms(); }
    void reset() {
        buf_.reset();
        last_ts_ms_.store(0, std::memory_order_relaxed);
    }

    using Stats = JitterBuffer<PcmFrame>::Stats;
    Stats stats() const { return buf_.stats(); }

private:
    JitterBuffer<PcmFrame> buf_;
    std::atomic<uint64_t> last_ts_ms_{0};
};
