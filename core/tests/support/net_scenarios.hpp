#pragma once

// Network-condition profiles for the integration gate, expressed in the
// author-facing terms (loss fraction, mean burst length, delay±jitter,
// capacity) and converted to the SFU fault-stage knobs. The profile values
// synthesize RMCAT RFC 8867/8869 and WebRTC full_stack_tests presets.
// Integration-tier only: includes the signaling server's fault-stage header.

#include "sfu_media_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>

namespace test_util {

// Mean loss = P(bad) * loss_in_bad with loss_in_good = 0, so with
// loss_in_bad = 1: good_to_bad / (good_to_bad + bad_to_good) = loss_fraction
// and mean burst length = 1 / bad_to_good.
inline driscord::sfu::BurstLossConfig burst_loss(double loss_fraction,
    double mean_burst_packets,
    uint64_t seed)
{
    driscord::sfu::BurstLossConfig config;
    if (loss_fraction <= 0.0 || loss_fraction >= 1.0) {
        return config;
    }
    config.bad_to_good = 1.0 / std::max(mean_burst_packets, 1.0);
    config.good_to_bad
        = config.bad_to_good * loss_fraction / (1.0 - loss_fraction);
    config.loss_in_good = 0.0;
    config.loss_in_bad = 1.0;
    config.seed = seed;
    return config;
}

inline driscord::sfu::LinkModelConfig link_model(int delay_ms,
    int jitter_ms,
    uint32_t capacity_kbps,
    size_t queue_packets,
    uint64_t seed)
{
    driscord::sfu::LinkModelConfig config;
    config.queue_delay_ms = delay_ms;
    config.delay_standard_deviation_ms = jitter_ms;
    config.link_capacity_kbps = capacity_kbps;
    config.queue_length_packets = queue_packets;
    config.seed = seed;
    return config;
}

// ---- Profiles ---------------------------------------------------------------

inline driscord::sfu::RtpFaultConfig clean_profile()
{
    return { };
}

inline driscord::sfu::RtpFaultConfig wifi_good_profile(uint64_t seed)
{
    return {
        .burst = burst_loss(0.002, 8.0, seed),
        .link = link_model(15, 5, 20'000, 100, seed),
    };
}

inline driscord::sfu::RtpFaultConfig wifi_burst_profile(uint64_t seed)
{
    return {
        .burst = burst_loss(0.03, 10.0, seed),
        .link = link_model(25, 15, 8'000, 50, seed),
    };
}

inline driscord::sfu::RtpFaultConfig lte_good_profile(uint64_t seed)
{
    return {
        .burst = burst_loss(0.005, 4.0, seed),
        .link = link_model(50, 20, 5'000, 200, seed),
    };
}

inline driscord::sfu::RtpFaultConfig lte_edge_profile(uint64_t seed)
{
    return {
        .burst = burst_loss(0.05, 8.0, seed),
        .link = link_model(120, 60, 800, 100, seed),
    };
}

// No random loss at all: a small queue on a slow link is what actually
// stresses rate control and pacing, harder than raw loss does.
inline driscord::sfu::RtpFaultConfig congested_profile(uint64_t seed)
{
    return {
        .link = link_model(100, 0, 250, 10, seed),
    };
}

inline driscord::sfu::RtpFaultConfig awful_profile(uint64_t seed)
{
    return {
        .burst = burst_loss(0.15, 15.0, seed),
        .link = link_model(300, 100, 300, 100, seed),
    };
}

inline driscord::sfu::RtpFaultConfig link_down_profile()
{
    driscord::sfu::RtpFaultConfig config;
    config.link_down = true;
    return config;
}

// ---- Timelines --------------------------------------------------------------

struct ScenarioStep {
    std::chrono::milliseconds duration { 0 };
    driscord::sfu::RtpFaultConfig config;
};

// Applies each step to the running fixture and holds it for its duration.
// Blocking; drive it from the test thread while media flows on its own
// threads. Fixture is any type exposing set_fault_config().
template <typename Fixture>
void run_scenario_timeline(Fixture& fixture,
    std::span<const ScenarioStep> steps)
{
    for (const auto& step : steps) {
        fixture.set_fault_config(step.config);
        std::this_thread::sleep_for(step.duration);
    }
}

} // namespace test_util
