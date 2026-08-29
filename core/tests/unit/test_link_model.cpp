#include "sfu_media_utils.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace {

using driscord::sfu::LinkModelConfig;
using driscord::sfu::LinkModelState;
using driscord::sfu::schedule_packet_departure;

std::vector<int64_t> departures(const LinkModelConfig& config,
    LinkModelState& state,
    int64_t start_us,
    int64_t spacing_us,
    size_t count,
    size_t packet_bytes = 1'200)
{
    std::vector<int64_t> result;
    for (size_t i = 0; i < count; ++i) {
        const int64_t arrival = start_us + static_cast<int64_t>(i) * spacing_us;
        const auto scheduled
            = schedule_packet_departure(config, state, arrival, packet_bytes);
        if (scheduled) {
            result.push_back(scheduled->departure_us);
        }
    }
    return result;
}

TEST(LinkModel, ZeroConfigIsIdentity)
{
    const LinkModelConfig config;
    ASSERT_FALSE(config.enabled());
    LinkModelState state;
    for (const int64_t arrival : { 0ll, 17ll, 1'000'000ll }) {
        const auto scheduled
            = schedule_packet_departure(config, state, arrival, 1'200);
        ASSERT_TRUE(scheduled.has_value());
        EXPECT_EQ(scheduled->departure_us, arrival);
        EXPECT_FALSE(scheduled->duplicate);
    }
    EXPECT_EQ(state.queued_packets, 0u);
    EXPECT_FALSE(state.initialized);
}

TEST(LinkModel, DeterministicPerSeedAndSeedsDiverge)
{
    LinkModelConfig config;
    config.queue_delay_ms = 40;
    config.delay_standard_deviation_ms = 15;
    config.seed = 7;

    LinkModelState first;
    LinkModelState second;
    const auto a = departures(config, first, 1'000, 20'000, 200);
    const auto b = departures(config, second, 1'000, 20'000, 200);
    EXPECT_EQ(a, b) << "same seed must produce a byte-exact schedule";

    config.seed = 8;
    LinkModelState third;
    const auto c = departures(config, third, 1'000, 20'000, 200);
    EXPECT_NE(a, c) << "different seeds must diverge";
}

TEST(LinkModel, SerializationDelayFollowsCapacity)
{
    LinkModelConfig config;
    config.link_capacity_kbps = 1'000;
    LinkModelState state;

    const auto lone = schedule_packet_departure(config, state, 0, 1'250);
    ASSERT_TRUE(lone.has_value());
    EXPECT_EQ(lone->departure_us, 10'000);

    const auto queued = schedule_packet_departure(config, state, 0, 1'250);
    ASSERT_TRUE(queued.has_value());
    EXPECT_EQ(queued->departure_us, 20'000);

    const auto later = schedule_packet_departure(config, state, 100'000, 1'250);
    ASSERT_TRUE(later.has_value());
    EXPECT_EQ(later->departure_us, 110'000);
}

TEST(LinkModel, BoundedQueueOverflows)
{
    LinkModelConfig config;
    config.queue_delay_ms = 1'000;
    config.queue_length_packets = 2;
    LinkModelState state;

    ASSERT_TRUE(schedule_packet_departure(config, state, 0, 1'200));
    ASSERT_TRUE(schedule_packet_departure(config, state, 0, 1'200));
    EXPECT_EQ(state.queued_packets, 2u);
    EXPECT_FALSE(schedule_packet_departure(config, state, 0, 1'200))
        << "third deferred packet must overflow the two-packet queue";

    state.queued_packets = 0;
    EXPECT_TRUE(schedule_packet_departure(config, state, 2'000'000, 1'200));
}

TEST(LinkModel, DeparturesStayMonotonicUnlessReorderingIsAllowed)
{
    LinkModelConfig config;
    config.queue_delay_ms = 30;
    config.delay_standard_deviation_ms = 25;
    config.seed = 3;

    LinkModelState state;
    const auto ordered = departures(config, state, 0, 5'000, 300);
    for (size_t i = 1; i < ordered.size(); ++i) {
        ASSERT_GE(ordered[i], ordered[i - 1])
            << "jitter must not double as a hidden reorder fault";
    }

    config.allow_reordering = true;
    bool inversion_found = false;
    for (uint64_t seed = 1; seed < 50 && !inversion_found; ++seed) {
        config.seed = seed;
        LinkModelState raw;
        const auto jittered = departures(config, raw, 0, 5'000, 100);
        for (size_t i = 1; i < jittered.size(); ++i) {
            if (jittered[i] < jittered[i - 1]) {
                inversion_found = true;
                break;
            }
        }
    }
    EXPECT_TRUE(inversion_found);
}

TEST(LinkModel, JitterCentersOnTheConfiguredDelay)
{
    LinkModelConfig config;
    config.queue_delay_ms = 80;
    config.delay_standard_deviation_ms = 20;
    config.allow_reordering = true;
    config.seed = 11;
    LinkModelState state;

    std::vector<double> delays_ms;
    for (size_t i = 0; i < 2'000; ++i) {
        const int64_t arrival = static_cast<int64_t>(i) * 1'000'000;
        const auto scheduled
            = schedule_packet_departure(config, state, arrival, 1'200);
        ASSERT_TRUE(scheduled.has_value());
        delays_ms.push_back(
            static_cast<double>(scheduled->departure_us - arrival) / 1'000.0);
    }
    const double mean
        = std::accumulate(delays_ms.begin(), delays_ms.end(), 0.0)
        / static_cast<double>(delays_ms.size());
    EXPECT_NEAR(mean, 80.0, 3.0);
    double variance = 0.0;
    for (const double delay : delays_ms) {
        variance += (delay - mean) * (delay - mean);
    }
    variance /= static_cast<double>(delays_ms.size());
    EXPECT_NEAR(std::sqrt(variance), 20.0, 5.0);
}

TEST(LinkModel, DuplicatesEveryNthPacket)
{
    LinkModelConfig config;
    config.duplicate_every_nth = 5;
    LinkModelState state;
    size_t duplicates = 0;
    for (size_t i = 0; i < 100; ++i) {
        const auto scheduled
            = schedule_packet_departure(config, state, 0, 1'200);
        ASSERT_TRUE(scheduled.has_value());
        if (scheduled->duplicate) {
            ++duplicates;
        }
    }
    EXPECT_EQ(duplicates, 20u);
}

}
