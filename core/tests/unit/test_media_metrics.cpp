#include "media_metrics.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <vector>

namespace {

using driscord::media::AudioReceiveStats;
using driscord::media::RtpReceiveStats;
using driscord::media::VideoReceiveStats;
using driscord::media::VoiceInboundRtpStats;

void set_env(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value == nullptr ? "" : value);
#else
    if (value == nullptr) {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

TEST(ConcealmentRate, ZeroTotalSamplesIsZeroNotNan)
{
    EXPECT_EQ(test_util::concealment_rate(500, AudioReceiveStats { }), 0.0);
}

TEST(ConcealmentRate, ComputesFractionOfPlayedOutSamples)
{
    AudioReceiveStats audio;
    audio.total_samples_received = 48'000;
    EXPECT_DOUBLE_EQ(test_util::concealment_rate(4'800, audio), 0.1);

    VoiceInboundRtpStats voice;
    voice.concealed_samples = 960;
    voice.audio.total_samples_received = 96'000;
    EXPECT_DOUBLE_EQ(test_util::concealment_rate(voice), 0.01);
}

TEST(ConcealmentBurstLength, MeanSamplesPerConcealmentRun)
{
    AudioReceiveStats audio;
    EXPECT_EQ(test_util::concealment_burst_length(960, audio), 0.0);
    audio.concealment_events = 4;
    EXPECT_DOUBLE_EQ(test_util::concealment_burst_length(960, audio), 240.0);
}

TEST(HarmonicFramerate, EvenPacingRecoversNominalFps)
{
    constexpr double kFrameDelay = 1.0 / 30.0;
    constexpr int kFrames = 300;
    VideoReceiveStats video;
    video.total_inter_frame_delay_seconds = kFrames * kFrameDelay;
    video.total_squared_inter_frame_delay_seconds
        = kFrames * kFrameDelay * kFrameDelay;
    EXPECT_NEAR(test_util::harmonic_framerate(video), 30.0, 1e-9);
}

TEST(HarmonicFramerate, SingleLongGapCollapsesTheValue)
{
    constexpr double kFrameDelay = 1.0 / 30.0;
    constexpr int kFrames = 299;
    VideoReceiveStats video;
    video.total_inter_frame_delay_seconds = kFrames * kFrameDelay + 2.0;
    video.total_squared_inter_frame_delay_seconds
        = kFrames * kFrameDelay * kFrameDelay + 4.0;
    // ~12 s of video, one 2 s freeze: harmonic fps must fall far below the
    // arithmetic 300 frames / 12 s = 25 fps.
    EXPECT_LT(test_util::harmonic_framerate(video), 5.0);
    VideoReceiveStats empty;
    EXPECT_EQ(test_util::harmonic_framerate(empty), 0.0);
}

TEST(FreezeRatio, FractionOfSessionSpentFrozen)
{
    VideoReceiveStats video;
    video.total_freezes_duration_seconds = 1.5;
    EXPECT_DOUBLE_EQ(test_util::freeze_ratio(video, 30.0), 0.05);
    EXPECT_EQ(test_util::freeze_ratio(video, 0.0), 0.0);
}

TEST(PlayoutSkew, RequiresBothTracksToHavePlayedOut)
{
    RtpReceiveStats audio;
    RtpReceiveStats video;
    EXPECT_FALSE(test_util::playout_skew_ms(audio, video).has_value());
    audio.estimated_playout_timestamp_ms = 1'000.0;
    EXPECT_FALSE(test_util::playout_skew_ms(audio, video).has_value());
    video.estimated_playout_timestamp_ms = 970.0;
    const auto skew = test_util::playout_skew_ms(audio, video);
    ASSERT_TRUE(skew.has_value());
    // Positive: audio ahead of video.
    EXPECT_DOUBLE_EQ(*skew, 30.0);
}

TEST(RepairRatio, RecoveredShareWithRfc3550Quirks)
{
    RtpReceiveStats rtp;
    EXPECT_DOUBLE_EQ(test_util::repair_ratio(rtp, 0), 1.0);
    rtp.retransmitted_packets_received = 50;
    EXPECT_DOUBLE_EQ(test_util::repair_ratio(rtp, 50), 0.5);
    // Negative cumulative loss (double-counted retransmissions) reads as
    // fully repaired, not as a division blow-up.
    EXPECT_DOUBLE_EQ(test_util::repair_ratio(rtp, -3), 1.0);
}

TEST(Percentile, NearestRank)
{
    std::vector<double> values;
    for (int i = 100; i >= 1; --i) {
        values.push_back(i);
    }
    EXPECT_DOUBLE_EQ(test_util::percentile(values, 50.0), 50.0);
    EXPECT_DOUBLE_EQ(test_util::percentile(values, 95.0), 95.0);
    EXPECT_DOUBLE_EQ(test_util::percentile(values, 100.0), 100.0);
    EXPECT_DOUBLE_EQ(test_util::percentile(values, 0.0), 1.0);
    EXPECT_EQ(test_util::percentile({ }, 50.0), 0.0);
}

TEST(Monotonicity, NonDecreasingSeries)
{
    EXPECT_TRUE(test_util::is_non_decreasing({ }));
    EXPECT_TRUE(test_util::is_non_decreasing({ 0.0, 0.0, 0.1, 0.4 }));
    EXPECT_FALSE(test_util::is_non_decreasing({ 0.0, 0.2, 0.1 }));
}

TEST(QualityGate, EnforceModeFailsAndSoftModeLogsOnly)
{
    set_env("DRISCORD_QUALITY_ENFORCE", nullptr);
    EXPECT_TRUE(test_util::gate_le("unit.pass", 1.0, 2.0));
    EXPECT_FALSE(test_util::gate_le("unit.fail", 3.0, 2.0));
    EXPECT_TRUE(test_util::gate_ge("unit.pass", 2.0, 1.0));
    EXPECT_FALSE(test_util::gate_ge("unit.fail", 0.5, 1.0));

    set_env("DRISCORD_QUALITY_ENFORCE", "soft");
    EXPECT_TRUE(test_util::gate_le("unit.soft", 3.0, 2.0));
    EXPECT_TRUE(test_util::gate_ge("unit.soft", 0.5, 1.0));
    set_env("DRISCORD_QUALITY_ENFORCE", nullptr);
}

TEST(SampleStats, CollectsRequestedCountAndStopsOnFailure)
{
    struct FakeSession {
        int calls = 0;
        int fail_at = -1;
        bool get_stats(std::function<void(int)> callback)
        {
            if (calls == fail_at) {
                return false;
            }
            callback(calls++);
            return true;
        }
    };

    FakeSession ok;
    const auto samples = test_util::sample_stats<int>(
        ok, 3, std::chrono::milliseconds { 0 });
    EXPECT_EQ(samples, (std::vector<int> { 0, 1, 2 }));

    FakeSession failing;
    failing.fail_at = 1;
    const auto partial = test_util::sample_stats<int>(
        failing, 3, std::chrono::milliseconds { 0 });
    EXPECT_EQ(partial, (std::vector<int> { 0 }));
}

} // namespace
