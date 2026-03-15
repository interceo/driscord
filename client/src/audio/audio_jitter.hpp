#pragma once

#include "utils/jitter.hpp"
#include "utils/opus_codec.hpp"
#include "utils/time.hpp"

#include <array>
#include <cstring>

inline constexpr uint32_t kDefaultJitterMs = 80;

// Duration of one Opus frame in milliseconds (20ms @ 48kHz).
inline constexpr int kPcmFrameMs = opus::kFrameSize * 1000 / opus::kSampleRate;

struct PcmFrame {
    std::array<float, opus::kFrameSize> samples{};
};

// Audio jitter buffer.
//
// Sequences packets by seq number. Timestamp is stored for A/V sync only
// (exposed via current_playback_ts / re_anchor).
//
// push() — network/decode thread.
// pop()  — audio callback thread.
class AudioJitter {
public:
    explicit AudioJitter(size_t target_delay_ms = kDefaultJitterMs, int /*sample_rate*/ = opus::kSampleRate)
        : buf_(static_cast<int>(target_delay_ms), kPcmFrameMs) {}

    void push(const float* mono_pcm, size_t frames, uint64_t seq, utils::WallTimestamp sender_ts = {}) {
        PcmFrame f;
        const size_t n = std::min(frames, static_cast<size_t>(opus::kFrameSize));
        std::memcpy(f.samples.data(), mono_pcm, n * sizeof(float));
        buf_.push(seq, std::move(f), sender_ts);
    }

    // Copies the next frame into out[0..frames). Returns number of valid
    // samples written (0 = not primed or packet missing → silence already set).
    size_t pop(float* out, size_t frames) {
        auto pkt = buf_.pop();
        if (!pkt) {
            std::memset(out, 0, frames * sizeof(float));
            return 0;
        }
        const size_t n = std::min(frames, static_cast<size_t>(opus::kFrameSize));
        std::memcpy(out, pkt->data.samples.data(), n * sizeof(float));
        if (n < frames) {
            std::memset(out + n, 0, (frames - n) * sizeof(float));
        }
        return n;
    }

    bool primed() const { return buf_.primed(); }
    size_t buffered_ms() const { return buf_.buffered_ms(); }
    utils::WallTimestamp current_playback_ts() const { return buf_.current_playback_ts(); }
    void re_anchor(utils::WallTimestamp ts) { buf_.re_anchor(ts); }
    void reset() { buf_.reset(); }

    using Stats = JitterBuffer<PcmFrame>::Stats;
    Stats stats() const { return buf_.stats(); }

private:
    JitterBuffer<PcmFrame> buf_;
};
