#include "wsola.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Crossfade length. Long enough that a splice is inaudible, short enough that
// the two halves are still correlated.
constexpr int kOverlapMs = 5;

// Human pitch, give or take. Searching outside this range finds matches that
// are technically better correlated but are not periods, and splicing there
// changes the perceived rhythm.
constexpr int kMinPitchHz = 60;
constexpr int kMaxPitchHz = 400;

} // namespace

Wsola::Wsola(const int sample_rate, const int channels)
    : channels_(std::max(1, channels))
    , overlap_(static_cast<size_t>(sample_rate) * kOverlapMs / 1000)
    , min_lag_(static_cast<size_t>(sample_rate) / kMaxPitchHz)
    , max_lag_(static_cast<size_t>(sample_rate) / kMinPitchHz)
{
}

size_t Wsola::search(const float* in,
    const size_t lo,
    const size_t hi,
    const size_t lag_step,
    const size_t decim) const
{
    const size_t stride = static_cast<size_t>(channels_) * decim;
    const size_t taps = overlap_ / decim;

    float ref_energy = 0.0f;
    for (size_t i = 0; i < taps; ++i) {
        const float v = in[i * stride];
        ref_energy += v * v;
    }

    size_t best = lo;
    float best_score = -2.0f;

    for (size_t lag = lo; lag <= hi; lag += lag_step) {
        const float* cand = in + lag * static_cast<size_t>(channels_);
        float dot = 0.0f;
        float cand_energy = 0.0f;
        for (size_t i = 0; i < taps; ++i) {
            const float a = in[i * stride];
            const float b = cand[i * stride];
            dot += a * b;
            cand_energy += b * b;
        }

        const float denom = std::sqrt(ref_energy * cand_energy);
        // Near-silence has no meaningful period; treat every lag as equally
        // good rather than letting numerical noise pick one.
        const float score = denom > 1e-12f ? dot / denom : 0.0f;
        if (score > best_score) {
            best_score = score;
            best = lag;
        }
    }

    return best;
}

size_t Wsola::best_lag(const float* in, const size_t frames) const
{
    const size_t last_lag = std::min(max_lag_, frames - overlap_);
    if (last_lag <= min_lag_) {
        return min_lag_;
    }

    // Two passes. A full-resolution sweep of every lag against every sample
    // costs around 160k multiply-adds, which is a real spike to take inside an
    // audio callback. The coarse pass narrows the answer at a fraction of the
    // cost, and the fine pass recovers the exact splice point — which is the
    // part that has to be right, since a lag off by a sample or two is what
    // turns an inaudible edit into a click.
    const size_t coarse = search(in, min_lag_, last_lag, kCoarseLagStep, kCoarseDecim);

    const size_t lo = coarse > min_lag_ + kCoarseLagStep ? coarse - kCoarseLagStep : min_lag_;
    const size_t hi = std::min(last_lag, coarse + kCoarseLagStep);
    return search(in, lo, hi, 1, 1);
}

size_t Wsola::compress(const float* in, const size_t frames, float* out) const
{
    if (frames < min_input_frames()) {
        return 0;
    }

    const size_t ch = static_cast<size_t>(channels_);
    const size_t ov = overlap_;
    const size_t lag = best_lag(in, frames);

    // Fade from the head of the input into the point one period later, then
    // continue from there. The period in between is what disappears.
    for (size_t i = 0; i < ov; ++i) {
        const float w = static_cast<float>(i) / static_cast<float>(ov);
        for (size_t c = 0; c < ch; ++c) {
            out[i * ch + c] = in[i * ch + c] * (1.0f - w)
                + in[(lag + i) * ch + c] * w;
        }
    }

    const size_t tail = frames - lag - ov;
    std::memcpy(out + ov * ch, in + (lag + ov) * ch, tail * ch * sizeof(float));

    return frames - lag;
}

size_t Wsola::expand(const float* in,
    const size_t frames,
    float* out,
    const size_t out_cap) const
{
    if (frames < min_input_frames()) {
        return 0;
    }

    const size_t ch = static_cast<size_t>(channels_);
    const size_t ov = overlap_;
    const size_t lag = best_lag(in, frames);

    if (frames + lag > out_cap) {
        return 0;
    }

    // Emit one period, then fade back to the start of the input and replay it.
    // The listener hears the same period twice, spliced where the waveform
    // already repeats.
    std::memcpy(out, in, lag * ch * sizeof(float));

    for (size_t i = 0; i < ov; ++i) {
        const float w = static_cast<float>(i) / static_cast<float>(ov);
        for (size_t c = 0; c < ch; ++c) {
            out[(lag + i) * ch + c] = in[(lag + i) * ch + c] * (1.0f - w)
                + in[i * ch + c] * w;
        }
    }

    const size_t tail = frames - ov;
    std::memcpy(out + (lag + ov) * ch, in + ov * ch, tail * ch * sizeof(float));

    return frames + lag;
}
