// Streaming quality under modelled network conditions.
//
// Every scenario below runs both receive pipelines — real Opus, real H.264 —
// through a seeded network model on a simulated clock, then measures what came
// out: PSNR/SSIM against the frame that was captured, segmental SNR and glitch
// count against the audio that was encoded, freezes, NetEq-style concealment
// rates, and the skew between the two.
//
// Nothing here sleeps and nothing is random, so a failure is reproducible and a
// number that moves is a real change in behaviour.

#include "quality_main.hpp"

using test_util::record;
using test_util::NetProfile;
using test_util::Phase;
using test_util::QualityReport;
using test_util::Scenario;

namespace {

// Ceilings, not targets. Each one is what the pipeline must not exceed on the
// named link; tightening a budget is a one-line diff.
struct Budget {
    double expand_rate;
    size_t glitches;
    double min_seg_snr_db;
    double min_psnr_db;
    double min_ssim;
    size_t freezes;
    int64_t max_abs_skew_ms;
    int64_t max_target_delay_ms;
};

// ITU-R BT.1359: audio may lead the picture by ~45 ms and lag it by ~125 ms
// before the mismatch is visible.
constexpr int64_t kAudioAheadToleranceMs = 45;
constexpr int64_t kAudioBehindToleranceMs = 125;

constexpr int64_t kSec = 1'000'000;

void check(const QualityReport& r, const Budget& b)
{
    EXPECT_LT(r.audio.expand_rate, b.expand_rate) << r.scenario << ": concealment";
    EXPECT_LE(r.audio.glitch_count, b.glitches) << r.scenario << ": splice artefacts";
    EXPECT_GT(r.audio.seg_snr_db, b.min_seg_snr_db) << r.scenario << ": audio fidelity";
    EXPECT_GT(r.audio_stats.packets_received, 100u) << r.scenario << ": stream never ran";
    EXPECT_LT(r.audio_stats.target_delay_ms, b.max_target_delay_ms)
        << r.scenario << ": buffer inflated";

    if (!r.video_enabled) {
        return;
    }
    EXPECT_GT(r.video.frames_matched, 50u) << r.scenario << ": too little video to judge";
    EXPECT_GT(r.video.psnr_avg_db, b.min_psnr_db) << r.scenario << ": picture fidelity";
    EXPECT_GT(r.video.ssim_avg, b.min_ssim) << r.scenario << ": structural similarity";
    EXPECT_LE(r.video.freeze_count, b.freezes) << r.scenario << ": stalls";
    EXPECT_LE(r.sync.max_abs_skew_ms, b.max_abs_skew_ms) << r.scenario << ": drift";
    EXPECT_GT(r.sync.samples, 20u) << r.scenario << ": no sync samples";
    if (r.sync.median_skew_ms < 0) {
        EXPECT_LT(-r.sync.median_skew_ms, kAudioAheadToleranceMs)
            << r.scenario << ": audio ahead of picture";
    } else {
        EXPECT_LT(r.sync.median_skew_ms, kAudioBehindToleranceMs)
            << r.scenario << ": audio behind picture";
    }
}

Scenario steady(std::string name, NetProfile p, int64_t seconds = 20)
{
    return Scenario { .name = std::move(name),
        .phases = { Phase { seconds * kSec, p, p } },
        .video_enabled = true,
        .width = 320,
        .height = 240,
        .fps = 30,
        .seed = 1 };
}

} // namespace

TEST(MediaQuality, CleanLink)
{
    const auto r = run_scenario(steady("clean", NetProfile::clean()));
    record(r);
    check(r, Budget { .expand_rate = 0.005, .glitches = 0, .min_seg_snr_db = 10.0,
        .min_psnr_db = 30.0, .min_ssim = 0.90, .freezes = 0,
        .max_abs_skew_ms = 60, .max_target_delay_ms = 140 });
}

TEST(MediaQuality, DegradedLink)
{
    const auto r = run_scenario(steady("degraded", NetProfile::degraded()));
    record(r);
    check(r, Budget { .expand_rate = 0.05, .glitches = 20, .min_seg_snr_db = 5.0,
        .min_psnr_db = 25.0, .min_ssim = 0.80, .freezes = 5,
        .max_abs_skew_ms = 130, .max_target_delay_ms = 220 });
}

TEST(MediaQuality, BadLink)
{
    const auto r = run_scenario(steady("bad", NetProfile::bad()));
    record(r);
    check(r, Budget { .expand_rate = 0.14, .glitches = 60, .min_seg_snr_db = 0.0,
        .min_psnr_db = 20.0, .min_ssim = 0.65, .freezes = 25,
        .max_abs_skew_ms = 220, .max_target_delay_ms = 330 });
}

TEST(MediaQuality, TerribleLink)
{
    const auto r = run_scenario(steady("terrible", NetProfile::terrible()));
    record(r);
    // The claim here is only that the pipeline degrades rather than collapses:
    // it still plays, still shows a picture, and does not run away on delay.
    EXPECT_GT(r.audio_stats.packets_received, 100u);
    EXPECT_GT(r.video.frames_rendered, 30u);
    EXPECT_LT(r.audio_stats.target_delay_ms, 500);
    EXPECT_LT(r.audio.expand_rate, 0.30);
}

// A delay step is what a route change or a congested uplink looks like. The
// buffer must grow to cover it and then come back down.
TEST(MediaQuality, DelaySpikeAndRecovery)
{
    NetProfile calm = NetProfile::clean();
    calm.delay_ms = 20;
    NetProfile storm = calm;
    storm.delay_ms = 320;
    storm.jitter_ms = 60;

    const Scenario sc { .name = "delay_spike",
        .phases = { Phase { 8 * kSec, calm, calm },
            Phase { 6 * kSec, storm, storm },
            Phase { 10 * kSec, calm, calm } },
        .video_enabled = true,
        .width = 320,
        .height = 240,
        .fps = 30,
        .seed = 2 };

    const auto r = run_scenario(sc);
    record(r);
    EXPECT_LT(r.audio.expand_rate, 0.12) << "spike was not absorbed";
    EXPECT_LE(r.sync.max_abs_skew_ms, 260) << "streams came apart across the spike";
    EXPECT_LT(r.audio_stats.target_delay_ms, 260)
        << "delay never came back down after the spike";
}

// A voice-only call must not pay video's buffering cost.
TEST(MediaQuality, VoiceOnlyStaysLowLatency)
{
    Scenario sc = steady("voice_only", NetProfile::clean());
    sc.video_enabled = false;
    const auto r = run_scenario(sc);
    record(r);
    EXPECT_LT(r.audio_stats.target_delay_ms, 90) << "voice latency inflated";
    EXPECT_LT(r.audio.expand_rate, 0.005);
    EXPECT_EQ(r.audio.glitch_count, 0u);
}

// Same seed, same numbers — the property the whole harness rests on.
TEST(MediaQuality, RunsAreReproducible)
{
    const auto a = run_scenario(steady("repro", NetProfile::bad(), 8));
    const auto b = run_scenario(steady("repro", NetProfile::bad(), 8));
    EXPECT_EQ(a.to_json().dump(), b.to_json().dump());
}

