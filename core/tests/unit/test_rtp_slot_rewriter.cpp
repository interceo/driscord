#include "rtp_slot_rewriter.hpp"
#include "sfu_media_utils.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace {

rtc::binary bytes(std::initializer_list<uint8_t> values)
{
    rtc::binary result;
    result.reserve(values.size());
    for (const uint8_t value : values) {
        result.push_back(std::byte { value });
    }
    return result;
}

uint8_t value(const rtc::binary& packet, size_t offset)
{
    return std::to_integer<uint8_t>(packet[offset]);
}

uint16_t value16(const rtc::binary& packet, size_t offset)
{
    return static_cast<uint16_t>((value(packet, offset) << 8)
        | value(packet, offset + 1));
}

uint32_t value32(const rtc::binary& packet, size_t offset)
{
    return (static_cast<uint32_t>(value(packet, offset)) << 24)
        | (static_cast<uint32_t>(value(packet, offset + 1)) << 16)
        | (static_cast<uint32_t>(value(packet, offset + 2)) << 8)
        | value(packet, offset + 3);
}

rtc::binary rtp_packet(uint16_t sequence, uint32_t timestamp)
{
    return bytes({ 0x80, 111,
        static_cast<uint8_t>(sequence >> 8),
        static_cast<uint8_t>(sequence),
        static_cast<uint8_t>(timestamp >> 24),
        static_cast<uint8_t>(timestamp >> 16),
        static_cast<uint8_t>(timestamp >> 8),
        static_cast<uint8_t>(timestamp),
        0xaa, 0xbb, 0xcc, 0xdd, 0x01 });
}

} // namespace

TEST(RtpHeaderRewrite, RewritesSsrcAndClearsOneByteMidOnly)
{
    auto packet = bytes({
        0x90,
        111,
        0,
        1,
        0,
        0,
        0,
        2,
        0xaa,
        0xbb,
        0xcc,
        0xdd,
        0xbe,
        0xde,
        0,
        1,
        0x40,
        '0',
        0x10,
        0x55,
        0x7f,
    });

    ASSERT_TRUE(driscord::sfu::rewrite_rtp_for_slot(
        packet, 0x11223344, uint8_t { 4 }));
    EXPECT_EQ(value(packet, 8), 0x11);
    EXPECT_EQ(value(packet, 9), 0x22);
    EXPECT_EQ(value(packet, 10), 0x33);
    EXPECT_EQ(value(packet, 11), 0x44);
    EXPECT_EQ(value(packet, 16), 0);
    EXPECT_EQ(value(packet, 17), 0);
    EXPECT_EQ(value(packet, 18), 0x10);
    EXPECT_EQ(value(packet, 19), 0x55);
    EXPECT_EQ(value(packet, 20), 0x7f);
}

TEST(RtpHeaderRewrite, SupportsTwoByteExtensions)
{
    auto packet = bytes({
        0x90,
        111,
        0,
        1,
        0,
        0,
        0,
        2,
        0,
        0,
        0,
        1,
        0x10,
        0x00,
        0,
        2,
        4,
        2,
        '1',
        '0',
        7,
        1,
        0x55,
        0,
    });

    ASSERT_TRUE(driscord::sfu::rewrite_rtp_for_slot(
        packet, 0x55667788, uint8_t { 4 }));
    for (size_t i = 16; i < 20; ++i) {
        EXPECT_EQ(value(packet, i), 0);
    }
    EXPECT_EQ(value(packet, 20), 7);
    EXPECT_EQ(value(packet, 21), 1);
    EXPECT_EQ(value(packet, 22), 0x55);
}

TEST(RtpHeaderRewrite, RejectsMalformedAndRtcpPackets)
{
    auto malformed = bytes({
        0x90,
        111,
        0,
        1,
        0,
        0,
        0,
        2,
        0xaa,
        0xbb,
        0xcc,
        0xdd,
        0xbe,
        0xde,
        0,
        2,
        0x40,
        '0',
        0,
        0,
    });
    EXPECT_FALSE(driscord::sfu::rewrite_rtp_for_slot(
        malformed, 0x11223344, uint8_t { 4 }));
    EXPECT_EQ(value(malformed, 8), 0xaa);

    auto rtcp = bytes({ 0x80, 200, 0, 1, 0, 0, 0, 0 });
    EXPECT_FALSE(driscord::sfu::rewrite_rtp_for_slot(
        rtcp, 0x11223344, std::nullopt));
}

TEST(RtpHeaderRewrite, StripsValidPaddingBeforeMediaHandlers)
{
    auto packet = bytes({
        0xa0,
        111,
        0,
        1,
        0,
        0,
        0,
        2,
        0xaa,
        0xbb,
        0xcc,
        0xdd,
        0x42,
        0,
        2,
    });

    ASSERT_TRUE(driscord::sfu::rewrite_rtp_for_slot(
        packet, 0x11223344, std::nullopt));
    ASSERT_EQ(packet.size(), 13u);
    EXPECT_EQ(value(packet, 0), 0x80);
    EXPECT_EQ(value(packet, 12), 0x42);
    EXPECT_EQ(value32(packet, 8), 0x11223344u);
}

TEST(RtpHeaderRewrite, RejectsInvalidPadding)
{
    auto zero_padding = bytes({
        0xa0,
        111,
        0,
        1,
        0,
        0,
        0,
        2,
        0xaa,
        0xbb,
        0xcc,
        0xdd,
        0,
    });
    EXPECT_FALSE(driscord::sfu::rewrite_rtp_for_slot(
        zero_padding, 0x11223344, std::nullopt));

    auto oversized_padding = bytes({
        0xa0,
        111,
        0,
        1,
        0,
        0,
        0,
        2,
        0xaa,
        0xbb,
        0xcc,
        0xdd,
        2,
    });
    EXPECT_FALSE(driscord::sfu::rewrite_rtp_for_slot(
        oversized_padding, 0x11223344, std::nullopt));
}

TEST(SfuMediaUtils, IgnoresPayloadTypesWithoutRtpMap)
{
    rtc::Description::Media audio(
        "audio 9 UDP/TLS/RTP/SAVPF 0 111", "voice");
    audio.addRtpMap(
        rtc::Description::Media::RtpMap("111 opus/48000/2"));

    EXPECT_NO_THROW(
        driscord::sfu::apply_forwarding_feedback_policy(audio));
    const auto format = driscord::sfu::primary_rtp_format(audio, "audio");
    EXPECT_EQ(format.payload_type, 111);
    EXPECT_EQ(format.clock_rate, 48'000u);
}

TEST(SfuMediaUtils, FallsBackWhenAllPayloadTypesAreUnmapped)
{
    rtc::Description::Media audio(
        "audio 9 UDP/TLS/RTP/SAVPF 0", "voice");

    EXPECT_NO_THROW(
        driscord::sfu::apply_forwarding_feedback_policy(audio));
    const auto format = driscord::sfu::primary_rtp_format(audio, "audio");
    EXPECT_EQ(format.payload_type, 111);
    EXPECT_EQ(format.clock_rate, 48'000u);
}

TEST(RtpSlotRewriter, KeepsTimelineContinuousAcrossPublisherSwitch)
{
    driscord::sfu::RtpSlotRewriter rewriter(48'000, 960);
    const auto first_generation = rewriter.begin_source();

    auto first = rtp_packet(65'000, 100'000);
    ASSERT_TRUE(rewriter.rewrite(
        first, first_generation, 0x11223344, std::nullopt));
    EXPECT_EQ(value16(first, 2), 65'000);
    EXPECT_EQ(value32(first, 4), 100'000u);

    auto next = rtp_packet(65'001, 100'960);
    ASSERT_TRUE(rewriter.rewrite(
        next, first_generation, 0x11223344, std::nullopt));
    EXPECT_EQ(value16(next, 2), 65'001);
    EXPECT_EQ(value32(next, 4), 100'960u);

    const auto second_generation = rewriter.begin_source();
    auto stale = rtp_packet(65'002, 101'920);
    EXPECT_FALSE(rewriter.rewrite(
        stale, first_generation, 0x11223344, std::nullopt));

    auto switched = rtp_packet(12, 4'000'000);
    ASSERT_TRUE(rewriter.rewrite(
        switched, second_generation, 0x11223344, std::nullopt));
    EXPECT_EQ(value16(switched, 2), 65'002);
    EXPECT_EQ(value32(switched, 4), 101'920u);

    auto retransmission = rtp_packet(12, 4'000'000);
    ASSERT_TRUE(rewriter.rewrite(
        retransmission, second_generation, 0x11223344, std::nullopt));
    EXPECT_EQ(value16(retransmission, 2), 65'002);
    EXPECT_EQ(value32(retransmission, 4), 101'920u);

    auto predates_switch = rtp_packet(11, 3'999'040);
    EXPECT_FALSE(rewriter.rewrite(
        predates_switch, second_generation, 0x11223344, std::nullopt));

    auto after_switch = rtp_packet(13, 4'000'960);
    ASSERT_TRUE(rewriter.rewrite(
        after_switch, second_generation, 0x11223344, std::nullopt));
    EXPECT_EQ(value16(after_switch, 2), 65'003);
    EXPECT_EQ(value32(after_switch, 4), 102'880u);
}

TEST(RtpFaults, DeterministicallyDropsAndReordersRtp)
{
    const driscord::sfu::RtpFaultConfig config {
        .drop_every_nth = 4,
        .reorder_every_nth = 2,
    };
    driscord::sfu::RtpFaultState state;

    auto first = driscord::sfu::apply_rtp_faults(
        config, state, rtp_packet(1, 960));
    ASSERT_TRUE(first.first);
    EXPECT_EQ(value16(*first.first, 2), 1);
    EXPECT_FALSE(first.second);

    auto delayed = driscord::sfu::apply_rtp_faults(
        config, state, rtp_packet(2, 1'920));
    EXPECT_FALSE(delayed.first);
    EXPECT_FALSE(delayed.second);

    auto reordered = driscord::sfu::apply_rtp_faults(
        config, state, rtp_packet(3, 2'880));
    ASSERT_TRUE(reordered.first);
    ASSERT_TRUE(reordered.second);
    EXPECT_EQ(value16(*reordered.first, 2), 3);
    EXPECT_EQ(value16(*reordered.second, 2), 2);

    auto dropped = driscord::sfu::apply_rtp_faults(
        config, state, rtp_packet(4, 3'840));
    EXPECT_FALSE(dropped.first);
    EXPECT_FALSE(dropped.second);
}

TEST(RtpFaults, NeverImpairRtcpControlPackets)
{
    const driscord::sfu::RtpFaultConfig config {
        .drop_every_nth = 1,
        .reorder_every_nth = 1,
    };
    driscord::sfu::RtpFaultState state;
    auto result = driscord::sfu::apply_rtp_faults(
        config, state, bytes({ 0x80, 200, 0, 1, 0, 0, 0, 0 }));
    EXPECT_TRUE(result.first);
    EXPECT_FALSE(result.second);
    EXPECT_EQ(state.packets_seen, 0u);
}
