#pragma once

#include <cstddef>

class Wsola {
public:
    Wsola(int sample_rate, int channels);

    size_t min_input_frames() const noexcept { return max_lag_ + overlap_; }

    size_t compress(const float* in, size_t frames, float* out) const;

    size_t expand(const float* in, size_t frames, float* out, size_t out_cap) const;

    size_t min_lag() const noexcept { return min_lag_; }
    size_t max_lag() const noexcept { return max_lag_; }

private:
    static constexpr size_t kCoarseLagStep = 4;
    static constexpr size_t kCoarseDecim = 2;

    size_t best_lag(const float* in, size_t frames) const;
    size_t search(const float* in, size_t lo, size_t hi,
        size_t lag_step, size_t decim) const;

    int channels_;
    size_t overlap_; // crossfade length, in frames
    size_t min_lag_; // shortest period considered (highest pitch)
    size_t max_lag_; // longest period considered (lowest pitch)
};
