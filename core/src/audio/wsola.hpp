#pragma once

#include <cstddef>

// Waveform-similarity overlap-add: changes how long a stretch of audio lasts
// without changing its pitch.
//
// The jitter buffer uses this to converge on its target delay. The alternative
// levers are both audible: dropping a packet punches a hole in the waveform,
// and repeating one produces a click at the seam. Here the splice is placed at
// the point of greatest waveform similarity — in practice a pitch period — so
// the edit lands where the signal is already repeating itself.
//
// Both operations adjust by exactly one pitch period per call. That is the
// natural quantum for this technique, and it keeps the caller's rate limiting
// honest: correcting more means calling again later, not editing harder.
//
// Stateless: each call is a pure function of its input. Audio is interleaved;
// the similarity search runs on the first channel and every channel is spliced
// at the same point, which is what keeps a stereo image intact.
class Wsola {
public:
    Wsola(int sample_rate, int channels);

    // Shortest input either operation accepts. A caller working in 20 ms Opus
    // frames needs two of them.
    size_t min_input_frames() const noexcept { return max_lag_ + overlap_; }

    // Writes a version of `in` that is one pitch period shorter.
    // Returns frames written, or 0 if the input was too short — in which case
    // the caller should pass the audio through untouched rather than force it.
    // `out` needs room for `frames` samples.
    size_t compress(const float* in, size_t frames, float* out) const;

    // Writes a version of `in` that is one pitch period longer.
    // `out` needs room for `frames + max_lag()` samples.
    size_t expand(const float* in, size_t frames, float* out, size_t out_cap) const;

    // Bounds on how much a single call can change the length.
    size_t min_lag() const noexcept { return min_lag_; }
    size_t max_lag() const noexcept { return max_lag_; }

private:
    // Coarse search granularity: every 4th lag, correlating every 2nd sample.
    // Together they cut the similarity search by roughly an order of magnitude
    // before the exact lag is pinned down at full resolution.
    static constexpr size_t kCoarseLagStep = 4;
    static constexpr size_t kCoarseDecim = 2;

    // Lag in [min_lag_, max_lag_] whose waveform best matches the head of `in`.
    size_t best_lag(const float* in, size_t frames) const;
    // Best lag within [lo, hi], stepping lags by `lag_step` and correlating
    // every `decim`-th sample.
    size_t search(const float* in, size_t lo, size_t hi,
        size_t lag_step, size_t decim) const;

    int channels_;
    size_t overlap_; // crossfade length, in frames
    size_t min_lag_; // shortest period considered (highest pitch)
    size_t max_lag_; // longest period considered (lowest pitch)
};
