
#include "sfu_media_utils.hpp"

#include <gtest/gtest.h>
#include <rtc/rtp.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

rtc::binary make_rtp(uint16_t sequence)
{
    rtc::binary packet(12, std::byte { 0 });
    packet[0] = std::byte { 0x80 };
    packet[1] = std::byte { 0x6F };
    packet[2] = std::byte(sequence >> 8);
    packet[3] = std::byte(sequence & 0xFF);
    return packet;
}

std::vector<bool> run(const driscord::sfu::RtpFaultConfig& config, int count)
{
    driscord::sfu::RtpFaultState state;
    std::vector<bool> delivered;
    delivered.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto result = driscord::sfu::apply_rtp_faults(
            config, state, make_rtp(static_cast<uint16_t>(i)));
        delivered.push_back(
            result.first.has_value() || result.second.has_value());
    }
    return delivered;
}

TEST(RtpBurstLoss, DisabledByDefault)
{
    driscord::sfu::RtpFaultConfig config;
    EXPECT_FALSE(config.burst.enabled());
    const auto pattern = run(config, 500);
    for (const bool delivered : pattern) {
        EXPECT_TRUE(delivered);
    }
}

TEST(RtpBurstLoss, IsDeterministicForAFixedSeed)
{
    driscord::sfu::RtpFaultConfig config;
    config.burst.good_to_bad = 0.05;
    config.burst.bad_to_good = 0.4;
    config.burst.loss_in_good = 0.0;
    config.burst.loss_in_bad = 0.9;
    config.burst.seed = 12345;

    const auto first = run(config, 1000);
    const auto second = run(config, 1000);
    EXPECT_EQ(first, second) << "same seed must reproduce the same losses";
}

TEST(RtpBurstLoss, DifferentSeedsDiverge)
{
    driscord::sfu::RtpFaultConfig a;
    a.burst.good_to_bad = 0.1;
    a.burst.bad_to_good = 0.3;
    a.burst.loss_in_bad = 1.0;
    a.burst.seed = 1;
    auto b = a;
    b.burst.seed = 2;
    EXPECT_NE(run(a, 1000), run(b, 1000));
}

TEST(RtpBurstLoss, LossesArriveInBurstsNotScattered)
{
    driscord::sfu::RtpFaultConfig config;
    config.burst.good_to_bad = 0.02;
    config.burst.bad_to_good = 0.15;
    config.burst.loss_in_good = 0.0;
    config.burst.loss_in_bad = 1.0;
    config.burst.seed = 777;

    const auto pattern = run(config, 5000);

    int dropped = 0;
    int drop_runs = 0;
    int longest_run = 0;
    int current_run = 0;
    bool prev_dropped = false;
    for (const bool delivered : pattern) {
        if (!delivered) {
            ++dropped;
            ++current_run;
            if (!prev_dropped) {
                ++drop_runs;
            }
            longest_run = std::max(longest_run, current_run);
        } else {
            current_run = 0;
        }
        prev_dropped = !delivered;
    }

    ASSERT_GT(dropped, 0) << "the model never dropped anything";
    const double mean_run
        = static_cast<double>(dropped) / static_cast<double>(drop_runs);
    EXPECT_GT(mean_run, 2.0)
        << "losses look scattered, not bursty (mean run " << mean_run << ")";
    EXPECT_GT(longest_run, 3);
}

TEST(RtpBurstLoss, NeverDropsRtcp)
{
    driscord::sfu::RtpFaultConfig config;
    config.burst.good_to_bad = 1.0;
    config.burst.bad_to_good = 0.0;
    config.burst.loss_in_good = 1.0;
    config.burst.loss_in_bad = 1.0;
    config.burst.seed = 5;

    rtc::binary rtcp { std::byte { 0x81 }, std::byte { 0xCB },
        std::byte { 0x00 }, std::byte { 0x01 }, std::byte { 0x11 },
        std::byte { 0x22 }, std::byte { 0x33 }, std::byte { 0x44 } };
    driscord::sfu::RtpFaultState state;
    auto result = driscord::sfu::apply_rtp_faults(config, state, rtcp);
    ASSERT_TRUE(result.first.has_value());
    EXPECT_EQ(*result.first, rtcp);
    EXPECT_EQ(state.packets_seen, 0u);
}

}
