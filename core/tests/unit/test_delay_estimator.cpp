#include <gtest/gtest.h>

#include "sync/delay_estimator.hpp"

#include <random>

using avsync::DelayEstimator;

namespace {

// Feeds `n` arrivals whose one-way delay is `base + jitter[i]`.
void feed(DelayEstimator& e, int64_t base_us, const std::vector<int64_t>& jitter_us)
{
    for (int64_t j : jitter_us) {
        e.observe(base_us + j);
    }
}

std::vector<int64_t> constant(size_t n, int64_t v)
{
    return std::vector<int64_t>(n, v);
}

} // namespace

TEST(DelayEstimator, NotReadyBeforeMinSamples)
{
    DelayEstimator e;
    for (size_t i = 0; i < DelayEstimator::kMinSamples - 1; ++i) {
        e.observe(1000);
        EXPECT_FALSE(e.ready());
        EXPECT_EQ(e.p95_variation_us(), -1);
    }
    e.observe(1000);
    EXPECT_TRUE(e.ready());
    EXPECT_GE(e.p95_variation_us(), 0);
}

TEST(DelayEstimator, PerfectLinkNeedsNoDelay)
{
    DelayEstimator e;
    feed(e, 5000, constant(200, 0));
    ASSERT_TRUE(e.ready());
    EXPECT_EQ(e.min_owd_us(), 5000);
    // One bucket of rounding, nothing more.
    EXPECT_LE(e.p95_variation_us(), 1000);
}

// The whole point of the rewrite: the absolute one-way delay carries an
// arbitrary clock offset between the two machines, and it must not influence
// the amount of buffering we ask for.
TEST(DelayEstimator, InvariantToClockOffset)
{
    std::mt19937 rng(7);
    std::vector<int64_t> jitter;
    for (int i = 0; i < 300; ++i) {
        jitter.push_back(static_cast<int64_t>(rng() % 30'000));
    }

    DelayEstimator near, far, behind;
    feed(near, 5'000, jitter);
    feed(far, 3'600'000'000, jitter); // sender's clock an hour ahead
    feed(behind, -3'600'000'000, jitter); // and an hour behind

    EXPECT_EQ(near.p95_variation_us(), far.p95_variation_us());
    EXPECT_EQ(near.p95_variation_us(), behind.p95_variation_us());
    // The offset itself lands wherever the clocks put it; only the difference
    // between the estimators is meaningful, and it is exactly the clock shift.
    EXPECT_EQ(far.min_owd_us() - near.min_owd_us(), 3'600'000'000 - 5'000);
    EXPECT_EQ(behind.min_owd_us() - near.min_owd_us(), -3'600'000'000 - 5'000);
}

TEST(DelayEstimator, MinTracksTheFastestArrival)
{
    DelayEstimator e;
    feed(e, 50'000, constant(50, 0));
    EXPECT_EQ(e.min_owd_us(), 50'000);

    e.observe(20'000); // one unusually quick packet
    EXPECT_EQ(e.min_owd_us(), 20'000);

    // And a slow one does not move it.
    e.observe(900'000);
    EXPECT_EQ(e.min_owd_us(), 20'000);
}

TEST(DelayEstimator, VariationTracksSpread)
{
    DelayEstimator tight, loose;
    std::mt19937 rng(11);

    for (int i = 0; i < 400; ++i) {
        tight.observe(10'000 + static_cast<int64_t>(rng() % 5'000)); // 5 ms spread
    }
    for (int i = 0; i < 400; ++i) {
        loose.observe(10'000 + static_cast<int64_t>(rng() % 80'000)); // 80 ms spread
    }

    EXPECT_LT(tight.p95_variation_us(), 10'000);
    EXPECT_GT(loose.p95_variation_us(), 50'000);
    EXPECT_LT(loose.p95_variation_us(), 90'000);
}

TEST(DelayEstimator, PercentileIgnoresRareOutliers)
{
    DelayEstimator e;
    for (int i = 0; i < 400; ++i) {
        e.observe(10'000); // 99% of arrivals are perfectly on time
    }
    for (int i = 0; i < 3; ++i) {
        e.observe(400'000); // a handful of very late ones
    }
    // p95 must not chase the tail.
    EXPECT_LE(e.p95_variation_us(), 5'000);
    // p100 sees it.
    EXPECT_GE(e.variation_us(100), 300'000);
}

TEST(DelayEstimator, SaturatesOnHugeSpread)
{
    DelayEstimator e;
    for (int i = 0; i < 100; ++i) {
        e.observe(1'000);
    }
    for (int i = 0; i < 100; ++i) {
        e.observe(60'000'000); // a minute late
    }
    // Clamped into the top bucket rather than overflowing the histogram.
    EXPECT_GT(e.p95_variation_us(), 400'000);
    EXPECT_LE(e.p95_variation_us(), 512'000);
}

// A steady extra delay is not jitter — it is absorbed by the minimum and costs
// no buffering at all. Only spread does.
TEST(DelayEstimator, ConstantExtraDelayCostsNothing)
{
    DelayEstimator e;
    for (int i = 0; i < 300; ++i) {
        e.observe(10'000 + 100'000); // consistently 100 ms slower, never varying
    }
    EXPECT_LE(e.p95_variation_us(), 1'000);
    EXPECT_EQ(e.min_owd_us(), 110'000);
}

// The sliding window has to forget: a link that was bad and recovered must not
// keep paying for its history forever.
TEST(DelayEstimator, WindowForgetsOldSamples)
{
    DelayEstimator e;
    std::mt19937 rng(3);
    for (size_t i = 0; i < DelayEstimator::kWindow; ++i) {
        e.observe(10'000 + static_cast<int64_t>(rng() % 150'000)); // erratic link
    }
    const int64_t bad = e.p95_variation_us();
    EXPECT_GT(bad, 50'000);

    for (size_t i = 0; i < DelayEstimator::kWindow; ++i) {
        e.observe(10'000); // link recovers completely
    }
    EXPECT_LT(e.p95_variation_us(), bad);
    EXPECT_LE(e.p95_variation_us(), 2'000);
}

TEST(DelayEstimator, ResetClearsEverything)
{
    DelayEstimator e;
    feed(e, 10'000, constant(100, 50'000));
    ASSERT_TRUE(e.ready());

    e.reset();
    EXPECT_FALSE(e.ready());
    EXPECT_EQ(e.p95_variation_us(), -1);
    EXPECT_EQ(e.sample_count(), 0u);

    feed(e, 7'000, constant(50, 0));
    EXPECT_EQ(e.min_owd_us(), 7'000);
}
