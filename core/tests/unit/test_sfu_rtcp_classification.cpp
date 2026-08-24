// One byte fixture per RTCP packet type the SFU can meet on its media path.
//
// The SFU never parses RTCP itself — libdatachannel handlers do — but three
// pieces of driscord code must still classify these bytes correctly on every
// packet: the RTP slot rewriter must refuse to rewrite them, and the
// deterministic fault injector must pass them through untouched even when it
// is configured to drop or reorder everything. The per-type byte fixtures
// follow mediasoup's worker/test/src/RTC/RTCP discipline: real header bytes,
// asserted field by field, compared byte for byte after the code under test
// ran.

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
            0x80, 0xC8, 0x00, 0x06, // V=2, RC=0, PT=SR, length 6
            0x11, 0x22, 0x33, 0x44, // sender SSRC
            0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // NTP timestamp
            0x00, 0x00, 0x03, 0xE8, // RTP timestamp
            0x00, 0x00, 0x00, 0x2A, // packet count
            0x00, 0x00, 0x10, 0x00, // octet count
        }) },
        { "receiver_report_201", make_packet({
            0x81, 0xC9, 0x00, 0x07, // V=2, RC=1, PT=RR, length 7
            0x11, 0x22, 0x33, 0x44, // reporter SSRC
            0x55, 0x66, 0x77, 0x88, // reportee SSRC
            0x00, 0x00, 0x00, 0x00, // fraction lost + cumulative lost
            0x00, 0x00, 0x00, 0x64, // extended highest sequence
            0x00, 0x00, 0x00, 0x05, // interarrival jitter
            0x00, 0x00, 0x00, 0x00, // last SR
            0x00, 0x00, 0x00, 0x00, // delay since last SR
        }) },
        { "sdes_cname_202", make_packet({
            0x81, 0xCA, 0x00, 0x03, // V=2, SC=1, PT=SDES, length 3
            0x11, 0x22, 0x33, 0x44, // SSRC
            0x01, 0x04, 't', 'e', 's', 't', // CNAME item
            0x00, 0x00, // terminator + pad
        }) },
        { "bye_203", make_packet({
            0x81, 0xCB, 0x00, 0x01, // V=2, SC=1, PT=BYE, length 1
            0x11, 0x22, 0x33, 0x44, // SSRC
        }) },
        { "app_204", make_packet({
            0x80, 0xCC, 0x00, 0x02, // V=2, subtype 0, PT=APP, length 2
            0x11, 0x22, 0x33, 0x44, // SSRC
            'd', 'r', 'i', 's', // name
        }) },
        { "generic_nack_205", make_packet({
            0x81, 0xCD, 0x00, 0x03, // V=2, FMT=1 (NACK), PT=RTPFB, length 3
            0x11, 0x22, 0x33, 0x44, // sender SSRC
            0x55, 0x66, 0x77, 0x88, // media SSRC
            0x00, 0x64, 0x00, 0x03, // PID 100, BLP 0b11
        }) },
        { "pli_206", make_packet({
            0x81, 0xCE, 0x00, 0x02, // V=2, FMT=1 (PLI), PT=PSFB, length 2
            0x11, 0x22, 0x33, 0x44, // sender SSRC
            0x55, 0x66, 0x77, 0x88, // media SSRC
        }) },
        { "fir_206", make_packet({
            0x84, 0xCE, 0x00, 0x04, // V=2, FMT=4 (FIR), PT=PSFB, length 4
            0x11, 0x22, 0x33, 0x44, // sender SSRC
            0x00, 0x00, 0x00, 0x00, // media SSRC (unused for FIR)
            0x55, 0x66, 0x77, 0x88, // FCI: target SSRC
            0x01, 0x00, 0x00, 0x00, // FCI: sequence + reserved
        }) },
        { "remb_206", make_packet({
            0x8F, 0xCE, 0x00, 0x05, // V=2, FMT=15 (AFB), PT=PSFB, length 5
            0x11, 0x22, 0x33, 0x44, // sender SSRC
            0x00, 0x00, 0x00, 0x00, // media SSRC (always 0 for REMB)
            'R', 'E', 'M', 'B',
            0x01, 0x06, 0x00, 0x01, // 1 SSRC, exp 1, mantissa 0x60001? (opaque)
            0x55, 0x66, 0x77, 0x88, // fed-back SSRC
        }) },
        { "xr_207", make_packet({
            0x80, 0xCF, 0x00, 0x01, // V=2, PT=XR, length 1
            0x11, 0x22, 0x33, 0x44, // originator SSRC
        }) },
        { "compound_sr_sdes", make_packet({
            // SR (empty) ...
            0x80, 0xC8, 0x00, 0x06,
            0x11, 0x22, 0x33, 0x44,
            0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x00, 0x00, 0x03, 0xE8,
            0x00, 0x00, 0x00, 0x2A,
            0x00, 0x00, 0x10, 0x00,
            // ... followed by SDES in the same datagram
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
        0x64, // V=2, PT=111, sequence 100
        0x00,
        0x00,
        0x03,
        0xE8, // timestamp
        0xAA,
        0xBB,
        0xCC,
        0xDD, // SSRC
        0x01,
        0x02,
        0x03,
        0x04, // payload
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
    // Drop and reorder literally every RTP packet; RTCP must still sail
    // through byte-identical, in order, with no packet counted.
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

    // Control: the same config does drop RTP.
    auto rtp_result
        = driscord::sfu::apply_rtp_faults(config, state, make_rtp_packet());
    EXPECT_FALSE(rtp_result.first.has_value());
    EXPECT_EQ(state.packets_seen, 1u);
}

} // namespace
