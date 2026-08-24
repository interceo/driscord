// Replays the committed rtpdump fixtures — real Opus/VP8 publisher traffic
// captured off the SFU's production forwarding path by test_rtpdump_capture —
// through the slot rewrite pipeline. Synthetic packets in the rewriter's own
// unit test pin the byte-level contract; these fixtures pin it against header
// shapes Google WebRTC actually emits (extensions, padding, real payload
// sizes), including a mid-stream publisher switch on the same slot.

#include "rtp_slot_rewriter.hpp"

#include "test/rtp_file_reader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

struct FixtureStream {
    uint32_t ssrc = 0;
    std::vector<rtc::binary> packets;
};

uint32_t read_u32(const rtc::binary& packet, size_t offset)
{
    return (uint32_t(std::to_integer<uint8_t>(packet[offset])) << 24)
        | (uint32_t(std::to_integer<uint8_t>(packet[offset + 1])) << 16)
        | (uint32_t(std::to_integer<uint8_t>(packet[offset + 2])) << 8)
        | std::to_integer<uint8_t>(packet[offset + 3]);
}

uint16_t read_u16(const rtc::binary& packet, size_t offset)
{
    return uint16_t((std::to_integer<uint8_t>(packet[offset]) << 8)
        | std::to_integer<uint8_t>(packet[offset + 1]));
}

// Loads the dominant-SSRC stream of a fixture; other SSRCs (bandwidth
// probing) ride along in the capture but are not the rewrite subject.
FixtureStream load_dominant_stream(const std::string& name)
{
    const std::string path
        = std::string(DRISCORD_TEST_FIXTURES_DIR) + "/" + name;
    std::unique_ptr<webrtc::test::RtpFileReader> reader(
        webrtc::test::RtpFileReader::Create(
            webrtc::test::RtpFileReader::kRtpDump, path));
    if (!reader) {
        return { };
    }
    std::map<uint32_t, std::vector<rtc::binary>> by_ssrc;
    webrtc::test::RtpPacket packet;
    while (reader->NextPacket(&packet)) {
        rtc::binary bytes(packet.length);
        std::memcpy(bytes.data(), packet.data, packet.length);
        by_ssrc[read_u32(bytes, 8)].push_back(std::move(bytes));
    }
    FixtureStream stream;
    for (auto& [ssrc, packets] : by_ssrc) {
        if (packets.size() > stream.packets.size()) {
            stream.ssrc = ssrc;
            stream.packets = std::move(packets);
        }
    }
    return stream;
}

void replay_through_slot_rewriter(const std::string& fixture,
    uint32_t clock_rate,
    uint32_t timestamp_step)
{
    const auto stream = load_dominant_stream(fixture);
    ASSERT_GE(stream.packets.size(), 50u)
        << "fixture " << fixture << " missing or too small";

    constexpr uint32_t kOutputSsrc = 0x5005cafe;
    constexpr std::optional<uint8_t> kMidExtensionId = 3;
    driscord::sfu::RtpSlotRewriter rewriter(clock_rate, timestamp_step);

    // First half: one publisher generation. Real traffic must be accepted
    // packet for packet, keep the slot SSRC, and preserve the publisher's
    // sequence/timestamp deltas exactly.
    const size_t half = stream.packets.size() / 2;
    auto generation = rewriter.begin_source();
    std::optional<uint16_t> previous_in_seq;
    std::optional<uint16_t> previous_out_seq;
    std::optional<uint32_t> previous_in_ts;
    std::optional<uint32_t> previous_out_ts;
    for (size_t i = 0; i < half; ++i) {
        rtc::binary packet = stream.packets[i];
        const uint16_t in_seq = read_u16(packet, 2);
        const uint32_t in_ts = read_u32(packet, 4);
        ASSERT_TRUE(rewriter.rewrite(
            packet, generation, kOutputSsrc, kMidExtensionId))
            << fixture << ": rewrite rejected real packet " << i;
        ASSERT_EQ(read_u32(packet, 8), kOutputSsrc);
        const uint16_t out_seq = read_u16(packet, 2);
        const uint32_t out_ts = read_u32(packet, 4);
        if (previous_in_seq) {
            const uint16_t in_delta = uint16_t(in_seq - *previous_in_seq);
            const uint16_t out_delta = uint16_t(out_seq - *previous_out_seq);
            ASSERT_EQ(in_delta, out_delta) << fixture << " packet " << i;
            ASSERT_EQ(uint32_t(in_ts - *previous_in_ts),
                uint32_t(out_ts - *previous_out_ts))
                << fixture << " packet " << i;
        }
        previous_in_seq = in_seq;
        previous_out_seq = out_seq;
        previous_in_ts = in_ts;
        previous_out_ts = out_ts;
    }

    // Publisher switch mid-stream: the slot is reassigned, the old
    // generation must be rejected, and the subscriber-facing timeline must
    // continue seamlessly (+1 sequence, +step timestamp) even though the
    // "new publisher" carries entirely unrelated sequence numbers.
    rewriter.end_source();
    const auto old_generation = generation;
    generation = rewriter.begin_source();
    {
        rtc::binary stale = stream.packets[half];
        ASSERT_FALSE(rewriter.rewrite(
            stale, old_generation, kOutputSsrc, kMidExtensionId));
    }
    bool first_after_switch = true;
    for (size_t i = half; i < stream.packets.size(); ++i) {
        rtc::binary packet = stream.packets[i];
        ASSERT_TRUE(rewriter.rewrite(
            packet, generation, kOutputSsrc, kMidExtensionId))
            << fixture << ": rewrite rejected post-switch packet " << i;
        const uint16_t out_seq = read_u16(packet, 2);
        const uint32_t out_ts = read_u32(packet, 4);
        if (first_after_switch) {
            ASSERT_EQ(out_seq, uint16_t(*previous_out_seq + 1))
                << fixture << ": sequence discontinuity across the switch";
            // The rewriter learns the input's real timestamp cadence (VP8 at
            // ~29.4 fps steps by ~3060, not the configured default), so pin
            // continuity, not the exact default step: strictly forward, by
            // no more than a second of media.
            const uint32_t ts_jump = out_ts - *previous_out_ts;
            ASSERT_GT(ts_jump, 0u)
                << fixture << ": timestamp went backwards across the switch";
            ASSERT_LE(ts_jump, clock_rate)
                << fixture << ": timestamp warped across the switch";
            first_after_switch = false;
        }
        previous_out_seq = out_seq;
        previous_out_ts = out_ts;
    }
}

TEST(RtpDumpReplay, VoiceFixtureSurvivesSlotRewriteAndPublisherSwitch)
{
    // Voice slots run at the Opus clock: see voice_router.cpp.
    replay_through_slot_rewriter("voice.rtpdump", 48'000, 960);
}

TEST(RtpDumpReplay, ScreenFixtureSurvivesSlotRewriteAndPublisherSwitch)
{
    // Video slots run at the 90 kHz RTP clock: see screen_router.cpp.
    replay_through_slot_rewriter("screen.rtpdump", 90'000, 3'000);
}

} // namespace
