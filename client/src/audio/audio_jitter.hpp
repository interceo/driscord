#pragma once

#include "utils/jitter.hpp"
#include "utils/opus_codec.hpp"
#include "utils/time.hpp"

#include <chrono>
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
    explicit AudioJitter(size_t target_delay_ms = kDefaultJitterMs, int sample_rate = opus::kSampleRate)
        : buf_(static_cast<int>(target_delay_ms)), sample_rate_(sample_rate) {}

    void push(std::vector<float> samples, uint64_t seq, utils::WallTimestamp sender_ts = {}) {
        if (samples.empty()) {
            return;
        }

        auto duration_us = static_cast<int64_t>(samples.size()) * 1'000'000 / sample_rate_;
        buf_.set_packet_duration(std::chrono::microseconds(duration_us));
        buf_.push(seq, PcmFrame{std::move(samples)}, sender_ts);
    }

    // Returns decoded samples (moved from internal packet), or empty if no data.
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
    int sample_rate_;
};
