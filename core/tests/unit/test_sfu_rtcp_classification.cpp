
#include "rtp_slot_rewriter.hpp"
#include "sfu_media_utils.hpp"

#include <gtest/gtest.h>
#include <rtc/rtp.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

rtc::binary make_packet(std::initializer_list<uint8_t> bytes)
{
    rtc::binary packet;
    packet.reserve(bytes.size());
    for (const uint8_t byte : bytes) {
        packet.push_back(static_cast<std::byte>(byte));
    }
    return packet;
}

struct RtcpFixture {
    const char* name;
    rtc::binary packet;
};

// clang-format off
const std::vector<RtcpFixture>& rtcp_fixtures()
{
    static const std::vector<RtcpFixture> fixtures {
        { "sender_report_200", make_packet({
            0x80, 0xC8, 0x00, 0x06,
            0x11, 0x22, 0x33, 0x44,
            0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x00, 0x00, 0x03, 0xE8,
            0x00, 0x00, 0x00, 0x2A,
            0x00, 0x00, 0x10, 0x00,
        }) },
        { "receiver_report_201", make_packet({
            0x81, 0xC9, 0x00, 0x07,
            0x11, 0x22, 0x33, 0x44,
            0x55, 0x66, 0x77, 0x88,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x64,
            0x00, 0x00, 0x00, 0x05,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        }) },
        { "sdes_cname_202", make_packet({
            0x81, 0xCA, 0x00, 0x03,
            0x11, 0x22, 0x33, 0x44,
            0x01, 0x04, 't', 'e', 's', 't',
            0x00, 0x00,
        }) },
        { "bye_203", make_packet({
            0x81, 0xCB, 0x00, 0x01,
            0x11, 0x22, 0x33, 0x44,
        }) },
        { "app_204", make_packet({
            0x80, 0xCC, 0x00, 0x02,
            0x11, 0x22, 0x33, 0x44,
            'd', 'r', 'i', 's',
        }) },
        { "generic_nack_205", make_packet({
            0x81, 0xCD, 0x00, 0x03,
            0x11, 0x22, 0x33, 0x44,
            0x55, 0x66, 0x77, 0x88,
            0x00, 0x64, 0x00, 0x03,
        }) },
        { "pli_206", make_packet({
            0x81, 0xCE, 0x00, 0x02,
            0x11, 0x22, 0x33, 0x44,
            0x55, 0x66, 0x77, 0x88,
        }) },
        { "fir_206", make_packet({
            0x84, 0xCE, 0x00, 0x04,
            0x11, 0x22, 0x33, 0x44,
            0x00, 0x00, 0x00, 0x00,
            0x55, 0x66, 0x77, 0x88,
            0x01, 0x00, 0x00, 0x00,
        }) },
        { "remb_206", make_packet({
            0x8F, 0xCE, 0x00, 0x05,
            0x11, 0x22, 0x33, 0x44,
            0x00, 0x00, 0x00, 0x00,
            'R', 'E', 'M', 'B',
            0x01, 0x06, 0x00, 0x01,
            0x55, 0x66, 0x77, 0x88,
        }) },
        { "xr_207", make_packet({
            0x80, 0xCF, 0x00, 0x01,
            0x11, 0x22, 0x33, 0x44,
        }) },
        { "compound_sr_sdes", make_packet({
            0x80, 0xC8, 0x00, 0x06,
            0x11, 0x22, 0x33, 0x44,
            0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x00, 0x00, 0x03, 0xE8,
            0x00, 0x00, 0x00, 0x2A,
            0x00, 0x00, 0x10, 0x00,
            0x81, 0xCA, 0x00, 0x03,
            0x11, 0x22, 0x33, 0x44,
            0x01, 0x04, 't', 'e', 's', 't',
            0x00, 0x00,
        }) },
    };
    return fixtures;
}
// clang-format on

rtc::binary make_rtp_packet()
{
    return make_packet({
        0x80,
        0x6F,
        0x00,
        0x64,
        0x00,
        0x00,
        0x03,
        0xE8,
        0xAA,
        0xBB,
        0xCC,
        0xDD,
        0x01,
        0x02,
        0x03,
        0x04,
    });
}

TEST(SfuRtcpClassification, EveryRtcpTypeIsRecognizedAsRtcp)
{
    for (const auto& fixture : rtcp_fixtures()) {
        EXPECT_TRUE(rtc::IsRtcp(fixture.packet)) << fixture.name;
    }
    EXPECT_FALSE(rtc::IsRtcp(make_rtp_packet()));
}

TEST(SfuRtcpClassification, TheRtpRewriterRefusesEveryRtcpTypeUntouched)
{
    for (const auto& fixture : rtcp_fixtures()) {
        rtc::binary packet = fixture.packet;
        EXPECT_FALSE(driscord::sfu::rewrite_rtp_for_slot(
            packet, 0x50607080, std::nullopt))
            << fixture.name;
        EXPECT_EQ(packet, fixture.packet)
            << fixture.name << ": a refused packet must not be modified";
    }

    rtc::binary rtp = make_rtp_packet();
    EXPECT_TRUE(driscord::sfu::rewrite_rtp_for_slot(rtp, 0x50607080,
        std::nullopt));
    EXPECT_NE(rtp, make_rtp_packet());
}

TEST(SfuRtcpClassification, FaultInjectionNeverImpairsAnyRtcpType)
{
    const driscord::sfu::RtpFaultConfig config {
        .drop_every_nth = 1,
        .reorder_every_nth = 1,
    };
    driscord::sfu::RtpFaultState state;
    for (const auto& fixture : rtcp_fixtures()) {
        auto result = driscord::sfu::apply_rtp_faults(
            config, state, fixture.packet);
        ASSERT_TRUE(result.first.has_value()) << fixture.name;
        EXPECT_EQ(*result.first, fixture.packet) << fixture.name;
        EXPECT_FALSE(result.second.has_value()) << fixture.name;
        EXPECT_EQ(state.packets_seen, 0u) << fixture.name;
    }

    auto rtp_result
        = driscord::sfu::apply_rtp_faults(config, state, make_rtp_packet());
    EXPECT_FALSE(rtp_result.first.has_value());
    EXPECT_EQ(state.packets_seen, 1u);
}

}
