#include <gtest/gtest.h>

#include "audio/wsola.hpp"

#include <cmath>
#include <numbers>
#include <random>
#include <vector>

namespace {

constexpr int kRate = 48000;

std::vector<float> sine(double hz, size_t frames, int channels = 1, double amp = 0.5)
{
    std::vector<float> v(frames * static_cast<size_t>(channels));
    for (size_t i = 0; i < frames; ++i) {
        const double s = amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate);
        for (int c = 0; c < channels; ++c) {
            v[i * static_cast<size_t>(channels) + static_cast<size_t>(c)] = static_cast<float>(s);
        }
    }
    return v;
}

// Dominant period of a signal, by autocorrelation over a plausible pitch range.
size_t dominant_period(const std::vector<float>& x, int channels = 1)
{
    const size_t n = x.size() / static_cast<size_t>(channels);
    const size_t lo = kRate / 400;
    const size_t hi = std::min<size_t>(kRate / 60, n / 2);
    size_t best = lo;
    double best_score = -1e300;
    for (size_t lag = lo; lag <= hi; ++lag) {
        double dot = 0.0, e = 0.0;
        for (size_t i = 0; i + lag < n; ++i) {
            dot += static_cast<double>(x[i * static_cast<size_t>(channels)])
                * x[(i + lag) * static_cast<size_t>(channels)];
            e += static_cast<double>(x[(i + lag) * static_cast<size_t>(channels)])
                * x[(i + lag) * static_cast<size_t>(channels)];
        }
        const double score = e > 0 ? dot / std::sqrt(e) : 0.0;
        if (score > best_score) {
            best_score = score;
            best = lag;
        }
    }
    return best;
}

double rms(const float* x, size_t n)
{
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) {
        s += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(s / static_cast<double>(n));
}

// Largest sample-to-sample step. A bad splice shows up here as a click.
double max_step(const float* x, size_t n)
{
    double m = 0.0;
    for (size_t i = 1; i < n; ++i) {
        m = std::max(m, std::abs(static_cast<double>(x[i]) - x[i - 1]));
    }
    return m;
}

} // namespace

TEST(Wsola, RejectsShortInput)
{
    Wsola w(kRate, 1);
    auto in = sine(200, w.min_input_frames() - 1);
    std::vector<float> out(4096);
    EXPECT_EQ(w.compress(in.data(), in.size(), out.data()), 0u);
    EXPECT_EQ(w.expand(in.data(), in.size(), out.data(), out.size()), 0u);
}

TEST(Wsola, CompressShortensByOnePeriod)
{
    Wsola w(kRate, 1);
    const size_t frames = 1920; // two 20 ms Opus frames
    auto in = sine(200, frames);
    std::vector<float> out(frames);

    const size_t n = w.compress(in.data(), frames, out.data());
    ASSERT_GT(n, 0u);
    EXPECT_LT(n, frames);
    EXPECT_GE(frames - n, w.min_lag());
    EXPECT_LE(frames - n, w.max_lag());
}

TEST(Wsola, ExpandLengthensByOnePeriod)
{
    Wsola w(kRate, 1);
    const size_t frames = 1920;
    auto in = sine(200, frames);
    std::vector<float> out(frames + w.max_lag());

    const size_t n = w.expand(in.data(), frames, out.data(), out.size());
    ASSERT_GT(n, frames);
    EXPECT_GE(n - frames, w.min_lag());
    EXPECT_LE(n - frames, w.max_lag());
}

TEST(Wsola, ExpandRespectsOutputCapacity)
{
    Wsola w(kRate, 1);
    const size_t frames = 1920;
    auto in = sine(200, frames);
    std::vector<float> out(frames); // no room for the inserted period
    EXPECT_EQ(w.expand(in.data(), frames, out.data(), out.size()), 0u);
}

// The point of searching for waveform similarity: the splice must land on a
// pitch period, so the tone comes out unchanged.
TEST(Wsola, CompressPreservesPitch)
{
    Wsola w(kRate, 1);
    const size_t frames = 2400;
    // 440 Hz is above the search range, so the splice lands on a multiple of
    // the period rather than the period itself — still a valid splice point,
    // and the pitch must survive it.
    for (double hz : { 110.0, 220.0, 330.0, 440.0 }) {
        auto in = sine(hz, frames);
        std::vector<float> out(frames);
        const size_t n = w.compress(in.data(), frames, out.data());
        ASSERT_GT(n, 0u) << hz;
        out.resize(n);

        const double before = static_cast<double>(dominant_period(in));
        const double after = static_cast<double>(dominant_period(out));
        EXPECT_NEAR(after, before, 0.06 * before) << "pitch shifted at " << hz << " Hz";
    }
}

TEST(Wsola, ExpandPreservesPitch)
{
    Wsola w(kRate, 1);
    const size_t frames = 2400;
    for (double hz : { 110.0, 220.0, 330.0, 440.0 }) {
        auto in = sine(hz, frames);
        std::vector<float> out(frames + w.max_lag());
        const size_t n = w.expand(in.data(), frames, out.data(), out.size());
        ASSERT_GT(n, 0u) << hz;
        out.resize(n);

        const double before = static_cast<double>(dominant_period(in));
        const double after = static_cast<double>(dominant_period(out));
        EXPECT_NEAR(after, before, 0.06 * before) << "pitch shifted at " << hz << " Hz";
    }
}

// The measured period must actually match theory inside the analysis range,
// otherwise the test above would pass on two equally wrong numbers.
TEST(Wsola, PeriodMeasurementIsSound)
{
    for (double hz : { 110.0, 220.0, 330.0 }) {
        const auto in = sine(hz, 2400);
        const double expected = kRate / hz;
        EXPECT_NEAR(static_cast<double>(dominant_period(in)), expected, 0.03 * expected)
            << hz << " Hz";
    }
}

TEST(Wsola, CompressPreservesLoudness)
{
    Wsola w(kRate, 1);
    const size_t frames = 2400;
    auto in = sine(200, frames);
    std::vector<float> out(frames);
    const size_t n = w.compress(in.data(), frames, out.data());
    ASSERT_GT(n, 0u);
    EXPECT_NEAR(rms(out.data(), n), rms(in.data(), frames), 0.05);
}

// A splice at the wrong place is a click. On a periodic signal the output must
// stay as smooth as the input.
TEST(Wsola, CompressIntroducesNoDiscontinuity)
{
    Wsola w(kRate, 1);
    const size_t frames = 2400;
    auto in = sine(200, frames);
    std::vector<float> out(frames);
    const size_t n = w.compress(in.data(), frames, out.data());
    ASSERT_GT(n, 0u);
    EXPECT_LT(max_step(out.data(), n), max_step(in.data(), frames) * 2.0);
}

TEST(Wsola, ExpandIntroducesNoDiscontinuity)
{
    Wsola w(kRate, 1);
    const size_t frames = 2400;
    auto in = sine(200, frames);
    std::vector<float> out(frames + w.max_lag());
    const size_t n = w.expand(in.data(), frames, out.data(), out.size());
    ASSERT_GT(n, 0u);
    EXPECT_LT(max_step(out.data(), n), max_step(in.data(), frames) * 2.0);
}

TEST(Wsola, HandlesSilenceWithoutProducingNoise)
{
    Wsola w(kRate, 1);
    const size_t frames = 2400;
    std::vector<float> in(frames, 0.0f);
    std::vector<float> out(frames + w.max_lag(), 1.0f);

    const size_t n = w.compress(in.data(), frames, out.data());
    ASSERT_GT(n, 0u);
    for (size_t i = 0; i < n; ++i) {
        EXPECT_FLOAT_EQ(out[i], 0.0f) << "sample " << i;
    }
}

TEST(Wsola, HandlesNoiseWithoutBlowingUp)
{
    Wsola w(kRate, 1);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    std::vector<float> in(2400);
    for (float& v : in) {
        v = d(rng);
    }
    std::vector<float> out(in.size() + w.max_lag());

    const size_t n = w.compress(in.data(), in.size(), out.data());
    ASSERT_GT(n, 0u);
    for (size_t i = 0; i < n; ++i) {
        EXPECT_LE(std::abs(out[i]), 0.75f);
    }
}

// Every channel must be spliced at the same point, or the stereo image tears.
TEST(Wsola, StereoChannelsStayAligned)
{
    Wsola w(kRate, 2);
    const size_t frames = 2400;
    auto in = sine(200, frames, 2);
    std::vector<float> out(frames * 2);

    const size_t n = w.compress(in.data(), frames, out.data());
    ASSERT_GT(n, 0u);
    for (size_t i = 0; i < n; ++i) {
        EXPECT_FLOAT_EQ(out[i * 2], out[i * 2 + 1]) << "channels diverged at " << i;
    }
}

TEST(Wsola, StereoExpandStaysAligned)
{
    Wsola w(kRate, 2);
    const size_t frames = 2400;
    auto in = sine(200, frames, 2);
    std::vector<float> out((frames + w.max_lag()) * 2);

    const size_t n = w.expand(in.data(), frames, out.data(), frames + w.max_lag());
    ASSERT_GT(n, 0u);
    for (size_t i = 0; i < n; ++i) {
        EXPECT_FLOAT_EQ(out[i * 2], out[i * 2 + 1]) << "channels diverged at " << i;
    }
}

// Repeated correction must stay stable — this is what a buffer converging on a
// new target actually does.
TEST(Wsola, RepeatedCompressionIsStable)
{
    Wsola w(kRate, 1);
    const size_t frames = 2400;
    auto buf = sine(200, frames);
    std::vector<float> out(frames + w.max_lag());

    for (int round = 0; round < 40; ++round) {
        const size_t n = w.compress(buf.data(), frames, out.data());
        ASSERT_GT(n, 0u) << "round " << round;
        // Refill to a constant length so the loop can continue.
        buf.assign(out.begin(), out.begin() + static_cast<long>(n));
        buf.resize(frames);
        auto fill = sine(200, frames - n);
        std::copy(fill.begin(), fill.end(), buf.begin() + static_cast<long>(n));

        EXPECT_LT(rms(buf.data(), frames), 1.0) << "round " << round;
    }
}
