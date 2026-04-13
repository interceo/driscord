#pragma once

#include "spinlock.hpp"
#include "time.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace utils {

// Non-thread-safe fixed-size sliding window of int64_t samples.
// Provides percentile computation over the last N recorded values.
// Used by ClockSkewEstimator and JitterBuffer to avoid duplicating
// the ring-buffer + sort-and-select pattern.
template <size_t N, size_t kMinSamples = 8>
class SlidingWindow {
public:
    void push(int64_t v)
    {
        samples_[write_ % N] = v;
        ++write_;
        if (count_ < N) {
            ++count_;
        }
    }

    // Returns the p-th percentile (0–100), or -1 if fewer than kMinSamples
    // samples have been recorded.
    int64_t percentile(int p) const
    {
        if (count_ < kMinSamples) {
            return -1;
        }
        std::array<int64_t, N> buf;
        for (size_t i = 0; i < count_; ++i) {
            buf[i] = samples_[(write_ - count_ + i) % N];
        }
        std::sort(buf.begin(), buf.begin() + static_cast<ptrdiff_t>(count_));
        return buf[count_ * static_cast<size_t>(p) / 100];
    }

    int64_t median() const { return percentile(50); }

    size_t count() const { return count_; }

    void reset()
    {
        samples_.fill(0);
        write_ = 0;
        count_ = 0;
    }

private:
    std::array<int64_t, N> samples_ { };
    size_t write_ = 0;
    size_t count_ = 0;
};

// Sliding-window median one-way-delay estimator.
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
//
// Thread-safe: update() and median_ms() may be called from different threads.
class ClockSkewEstimator {
    static constexpr size_t kWindow = 64;

public:
    void update(utils::WallTimestamp sender_ts)
    {
        const int64_t delta_ms = utils::WallElapsedMs(sender_ts);
        std::scoped_lock lk(mu_);
        window_.push(delta_ms);
    }

    // Returns median delay (ms), or -1 if fewer than 8 samples.
    int64_t median_ms() const
    {
        std::scoped_lock lk(mu_);
        return window_.median();
    }

    int64_t percentile_ms(int p) const
    {
        std::scoped_lock lk(mu_);
        return window_.percentile(p);
    }

    void reset()
    {
        std::scoped_lock lk(mu_);
        window_.reset();
    }

private:
    mutable utils::SpinLock mu_;
    SlidingWindow<kWindow> window_;
};

} // namespace utils
