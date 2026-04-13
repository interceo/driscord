#pragma once

#include "slot_ring.hpp"
#include "time.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace utils {

// Returns the p-th percentile (0–100) of n elements in O(n) average time.
// Modifies the array in place (quickselect).
template <typename T>
T quickselect_percentile(T* first, size_t n, int p)
{
    assert(p >= 0 && p <= 100);
    T* k = first + n * static_cast<size_t>(p) / 100;
    std::nth_element(first, k, first + n);
    return *k;
}

// Sliding-window median one-way-delay estimator. Not thread-safe — callers
// must ensure mutual exclusion (e.g. JitterBuffer's own mutex).
//
// Each call to update() records (WallNow() - sender_ts) in milliseconds.
// median_ms() returns the median of the last kWindow samples, which
// approximates OWD + clock_skew for that stream.
//
// When comparing two streams from the same sender the skew cancels.
// For streams from different senders, subtracting their medians gives the
// relative clock drift:
//
//   corrected_drift = raw_drift - (audio_median - video_median)
class ClockSkewEstimator {
    static constexpr size_t kWindow = 64;
    static constexpr size_t kMinSamples = 8;

public:
    void update(utils::WallTimestamp sender_ts)
    {
        ring_.push(seq_++, utils::WallElapsedMs(sender_ts));
    }

    // Returns median delay (ms), or -1 if fewer than kMinSamples samples.
    int64_t median_ms() const { return percentile(50); }

    int64_t percentile_ms(int p) const { return percentile(p); }

    void reset()
    {
        ring_.reset();
        seq_ = 0;
    }

private:
    int64_t percentile(int p) const
    {
        const size_t n = ring_.size();
        if (n < kMinSamples) {
            return -1;
        }
        std::array<int64_t, kWindow> buf;
        size_t i = 0;
        ring_.for_each_occupied([&](const auto& slot) { buf[i++] = slot.data; });
        return quickselect_percentile(buf.data(), i, p);
    }

    SlotRing<int64_t, kWindow> ring_;
    uint64_t seq_ = 0;
};

} // namespace utils
