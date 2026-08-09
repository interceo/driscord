// Alternative playout-delay policies on identical traffic.
//
// The shipped policy is not obviously right, and "it sounds fine" is not a
// comparison. Same seed, same packets, same codecs — only the rule that decides
// how much to buffer changes, so the differences that come out are that rule's.

#include "alt_policies.hpp"
#include "quality_main.hpp"

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using test_util::EwmaSpikePolicy;
using test_util::FixedDelayPolicy;
using test_util::NetProfile;
using test_util::Phase;
using test_util::QualityReport;
using test_util::Scenario;

namespace {

constexpr int64_t kSec = 1'000'000;

struct Candidate {
    std::string name;
    test_util::PolicyFactory factory;
};

std::vector<Candidate> candidates()
{
    return {
        { "default", { } }, // MediaClock's own
        { "fixed-60", [] { return std::make_unique<FixedDelayPolicy>(60); } },
        { "fixed-120", [] { return std::make_unique<FixedDelayPolicy>(120); } },
        { "fixed-200", [] { return std::make_unique<FixedDelayPolicy>(200); } },
        { "ewma-spike", [] { return std::make_unique<EwmaSpikePolicy>(); } },
    };
}

// Sum of what the listener actually hears go wrong.
double artefacts(const QualityReport& r)
{
    return r.audio.expand_rate + r.audio.underrun_rate
        + static_cast<double>(r.audio.glitch_count) / 1000.0;
}

void print_table(const std::string& title, const std::vector<QualityReport>& rs,
    const std::vector<Candidate>& cs)
{
    std::cout << "\n" << title << "\n"
              << std::left << std::setw(12) << "policy"
              << std::right << std::setw(12) << "delay(ms)"
              << std::setw(14) << "expand"
              << std::setw(12) << "underrun"
              << std::setw(10) << "glitch"
              << std::setw(12) << "segSNR"
              << std::setw(10) << "skew\n";
    for (size_t i = 0; i < rs.size(); ++i) {
        std::cout << std::left << std::setw(12) << cs[i].name
                  << std::right << std::setw(12) << rs[i].audio_stats.target_delay_ms
                  << std::setw(14) << rs[i].audio.expand_rate
                  << std::setw(12) << rs[i].audio.underrun_rate
                  << std::setw(10) << rs[i].audio.glitch_count
                  << std::setw(12) << rs[i].audio.seg_snr_db
                  << std::setw(10) << rs[i].sync.median_skew_ms << "\n";
    }
}

std::vector<QualityReport> run_all(const Scenario& sc, const std::vector<Candidate>& cs)
{
    std::vector<QualityReport> out;
    out.reserve(cs.size());
    for (const auto& c : cs) {
        out.push_back(run_scenario(sc, c.factory));
    }
    return out;
}

} // namespace

// The bar the shipped policy has to clear: no alternative may be better on both
// axes at once. Being beaten on latency by a policy that glitches more, or on
// glitches by one that buffers longer, is a trade — being beaten on both is a
// defect.
TEST(PlayoutPolicies, DefaultIsNotDominated)
{
    const auto cs = candidates();
    for (const auto& profile :
        { NetProfile::degraded(), NetProfile::bad() }) {
        const Scenario sc { .name = "policy_ab",
            .phases = { Phase { 20 * kSec, profile, profile } },
            .video_enabled = true,
            .width = 320,
            .height = 240,
            .fps = 30,
            .seed = 7 };

        const auto rs = run_all(sc, cs);
        print_table("loss " + std::to_string(profile.loss_pct) + "% / delay "
                + std::to_string(profile.delay_ms) + "ms / jitter "
                + std::to_string(profile.jitter_ms) + "ms",
            rs, cs);

        const QualityReport& def = rs[0];
        for (size_t i = 1; i < rs.size(); ++i) {
            const bool better_quality = artefacts(rs[i]) < artefacts(def) - 1e-9;
            const bool lower_delay
                = rs[i].audio_stats.target_delay_ms < def.audio_stats.target_delay_ms;
            EXPECT_FALSE(better_quality && lower_delay)
                << cs[i].name << " beats the default on both artefacts and delay";
        }
    }
}

// A fixed buffer that is too small cannot absorb jitter; the adaptive one must
// visibly do better than the naive baseline it replaced.
TEST(PlayoutPolicies, BeatsAnUndersizedFixedBuffer)
{
    const auto cs = candidates();
    const auto p = NetProfile::bad();
    const Scenario sc { .name = "vs_fixed60",
        .phases = { Phase { 20 * kSec, p, p } },
        .video_enabled = false,
        .width = 320,
        .height = 240,
        .fps = 30,
        .seed = 11 };

    const auto def = run_scenario(sc, cs[0].factory);
    const auto fixed60 = run_scenario(sc, cs[1].factory);
    EXPECT_LT(def.audio.expand_rate, fixed60.audio.expand_rate)
        << "adaptive buffer conceals more than a fixed 60 ms one";
}

// And a fixed buffer large enough to be safe pays for it in latency.
TEST(PlayoutPolicies, BeatsAnOversizedFixedBufferOnLatency)
{
    const auto cs = candidates();
    const auto p = NetProfile::degraded();
    const Scenario sc { .name = "vs_fixed200",
        .phases = { Phase { 20 * kSec, p, p } },
        .video_enabled = false,
        .width = 320,
        .height = 240,
        .fps = 30,
        .seed = 13 };

    const auto def = run_scenario(sc, cs[0].factory);
    const auto fixed200 = run_scenario(sc, cs[3].factory);
    EXPECT_LT(def.audio_stats.target_delay_ms, fixed200.audio_stats.target_delay_ms)
        << "adaptive buffer is no quicker than a fixed 200 ms one";
}
