#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace avsync {

// Estimates how much playout delay a stream needs, from the arrival pattern of
// its packets.
//
// The input is a raw one-way delay: local_arrival_us - sender_ts_us. That value
// contains the offset between two machines' clocks, which is arbitrary and can
// be hours. Only its *variation* says anything about the network. So this
// tracks the running minimum — the closest thing to a delay-free transit we
// have seen, and therefore the best available estimate of the pure clock
// offset — and reports the spread above it.
//
// This is the part the previous implementation got wrong: it fed the absolute
// one-way delay into a percentile, so a peer whose clock ran 200 ms ahead got
// 200 ms of playout delay on a perfect link.
//
// Not thread-safe: one instance belongs to one producer thread.
class DelayEstimator {
public:
    // Samples retained. At 50 packets/s this is ~10 s of audio history.
    static constexpr size_t kWindow = 512;
    // Below this the estimates are noise and ready() stays false.
    static constexpr size_t kMinSamples = 16;

    void observe(const int64_t owd_us) noexcept
    {
        update_min(owd_us);

        int64_t variation = owd_us - min_owd_us();
        if (variation < 0) {
            variation = 0;
        }
        size_t bucket = static_cast<size_t>(variation / kBucketUs);
        if (bucket >= kBuckets) {
            bucket = kBuckets - 1; // saturating: anything this late is "very late"
        }

        if (count_ == kWindow) {
            --histogram_[ring_[head_]];
        } else {
            ++count_;
        }
        ring_[head_] = static_cast<uint16_t>(bucket);
        ++histogram_[bucket];
        head_ = (head_ + 1) % kWindow;
    }

    bool ready() const noexcept { return count_ >= kMinSamples; }

    // Best estimate of the constant part of the one-way delay: clock offset
    // plus whatever fixed transit cost this stream carries. Meaningless in
    // isolation; only differences between streams and samples matter.
    int64_t min_owd_us() const noexcept
    {
        return current_min_ < previous_min_ ? current_min_ : previous_min_;
    }

    // Spread above min_owd_us() that covers `percent` of recent arrivals.
    // Returns -1 until ready().
    int64_t variation_us(const int percent) const noexcept
    {
        if (!ready()) {
            return -1;
        }
        const size_t threshold = (count_ * static_cast<size_t>(percent) + 99) / 100;
        size_t seen = 0;
        for (size_t b = 0; b < kBuckets; ++b) {
            seen += histogram_[b];
            if (seen >= threshold) {
                // Upper edge of the bucket: rounding down here would under-size
                // the buffer by up to a full bucket every time.
                return static_cast<int64_t>(b + 1) * kBucketUs;
            }
        }
        return static_cast<int64_t>(kBuckets) * kBucketUs;
    }

    int64_t p95_variation_us() const noexcept { return variation_us(95); }

    size_t sample_count() const noexcept { return count_; }

    void reset() noexcept
    {
        histogram_.fill(0);
        ring_.fill(0);
        head_ = 0;
        count_ = 0;
        current_min_ = kUnset;
        previous_min_ = kUnset;
        block_count_ = 0;
    }

private:
    static constexpr int64_t kBucketUs = 1000; // 1 ms resolution
    static constexpr size_t kBuckets = 512; // up to ~512 ms of spread
    static constexpr int64_t kUnset = INT64_MAX;

    // Two-block running minimum: the live block accumulates, the previous one
    // keeps the estimate honest while it fills. The effective window is between
    // one and two blocks, at O(1) per sample and no history to scan.
    void update_min(const int64_t owd_us) noexcept
    {
        if (owd_us < current_min_) {
            current_min_ = owd_us;
        }
        if (++block_count_ >= kWindow) {
            previous_min_ = current_min_;
            current_min_ = kUnset;
            block_count_ = 0;
        }
    }

    std::array<uint16_t, kBuckets> histogram_ { };
    std::array<uint16_t, kWindow> ring_ { };
    size_t head_ = 0;
    size_t count_ = 0;

    int64_t current_min_ = kUnset;
    int64_t previous_min_ = kUnset;
    size_t block_count_ = 0;
};

} // namespace avsync
