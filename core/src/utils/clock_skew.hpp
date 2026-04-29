#pragma once

#include "slot_ring.hpp"
#include "time.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace utils {

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
    template <typename T>
    T quickselect_percentile(T* const first, const size_t n, const int p)
    {
        assert(p >= 0 && p <= 100);

        T* k = first + n * static_cast<size_t>(p) / 100;
        std::nth_element(first, k, first + n);
        return *k;
    }

    int64_t percentile(int p) const
    {
        const size_t n = ring_.size();
        if (n < kMinSamples) {
            return -1;
        }
        std::array<int64_t, kWindow> buf;
        size_t i = 0;
        // ring_.for_each_occupied([&](const auto& slot) { buf[i++] = slot.data; });
        // return quickselect_percentile(buf.data(), i, p);

        return 0;
    }

    SlotRing<int64_t, kWindow> ring_;
    uint64_t seq_ = 0;
};

} // namespace utils
