#pragma once

#include "utils/jitter.hpp"
#include "utils/opus_codec.hpp"
#include "utils/time.hpp"

#include <chrono>
#include <cstring>
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

    void push(const float* mono_pcm, size_t frames, uint64_t seq, utils::WallTimestamp sender_ts = {}) {
        const size_t n = std::min(frames, static_cast<size_t>(opus::kFrameSize));

        auto duration_us = static_cast<int64_t>(n) * 1'000'000 / sample_rate_;
        buf_.set_packet_duration(std::chrono::microseconds(duration_us));

        PcmFrame f;
        f.samples.assign(mono_pcm, mono_pcm + n);
        buf_.push(seq, std::move(f), sender_ts);
    }

    // Writes up to `frames` samples into out.
    // Returns number of valid samples written (0 = no data, out zeroed).
    size_t pop(float* out, size_t frames) {
        std::memset(out, 0, frames * sizeof(float));
        auto pkt = buf_.pop();
        if (!pkt || pkt->data.samples.empty()) {
            return 0;
        }
        const size_t n = std::min(frames, pkt->data.samples.size());
        std::memcpy(out, pkt->data.samples.data(), n * sizeof(float));
        return n;
    }

    size_t buffered_ms() const { return buf_.buffered_ms(); }
    void reset() { buf_.reset(); }

    using Stats = JitterBuffer<PcmFrame>::Stats;
    Stats stats() const { return buf_.stats(); }

private:
    JitterBuffer<PcmFrame> buf_;
    int sample_rate_;
};
