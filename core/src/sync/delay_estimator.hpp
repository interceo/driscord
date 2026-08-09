#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace avsync {

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

    int64_t min_owd_us() const noexcept
    {
        return current_min_ < previous_min_ ? current_min_ : previous_min_;
    }

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
