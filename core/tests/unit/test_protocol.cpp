#include <gtest/gtest.h>

#include "utils/enum_strings.hpp"
#include "utils/protocol.hpp"
#include "utils/signaling_protocol.hpp"
#include "channel_labels.hpp"

#include <cstring>
#include <string>
#include <variant>

using namespace std::chrono_literals;

// ---- AudioHeader ----

TEST(AudioHeader, Roundtrip)
{
    protocol::AudioHeader src;
    src.seq = 42;
    src.flags = protocol::flags::kTalkspurtStart;
    src.sender_ts_us = 123456789;

    uint8_t buf[protocol::AudioHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::AudioHeader::deserialize(buf);
    EXPECT_EQ(dst.seq, 42u);
    EXPECT_EQ(dst.flags, protocol::flags::kTalkspurtStart);
    EXPECT_EQ(dst.sender_ts_us, 123456789);
}

TEST(AudioHeader, WireSize)
{
    EXPECT_EQ(protocol::AudioHeader::kWireSize, 16u);
}

TEST(AudioHeader, ZeroValues)
{
    protocol::AudioHeader src { };
    uint8_t buf[protocol::AudioHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::AudioHeader::deserialize(buf);
    EXPECT_EQ(dst.seq, 0u);
    EXPECT_EQ(dst.flags, 0u);
    EXPECT_EQ(dst.sender_ts_us, 0);
}

TEST(AudioHeader, MaxSeq)
{
    protocol::AudioHeader src;
    src.seq = UINT32_MAX;
    src.sender_ts_us = 0;

    uint8_t buf[protocol::AudioHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::AudioHeader::deserialize(buf);
    EXPECT_EQ(dst.seq, UINT32_MAX);
    EXPECT_EQ(dst.sender_ts_us, 0);
}

// The sender clock starts at zero each process, so a receiver that boots
// mid-call sees timestamps far above its own elapsed time; nothing may assume
// the value is small or non-negative-bounded.
TEST(AudioHeader, LargeSenderTimestamp)
{
    protocol::AudioHeader src;
    src.seq = 1;
    src.sender_ts_us = 9'000'000'000'000LL; // ~104 days

    uint8_t buf[protocol::AudioHeader::kWireSize] { };
    src.serialize(buf);

    EXPECT_EQ(protocol::AudioHeader::deserialize(buf).sender_ts_us,
        9'000'000'000'000LL);
}

// ---- VideoHeader ----

TEST(VideoHeader, Roundtrip)
{
    protocol::VideoHeader src;
    src.width = 1920;
    src.height = 1080;
    src.sender_ts_us = 9999999;
    src.bitrate_kbps = 6000;
    src.frame_duration_us = 16667;
    src.flags = protocol::flags::kKeyframe;
    src.codec = protocol::VideoCodec::H264;

    uint8_t buf[protocol::VideoHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::VideoHeader::deserialize(buf);
    EXPECT_EQ(dst.width, 1920u);
    EXPECT_EQ(dst.height, 1080u);
    EXPECT_EQ(dst.sender_ts_us, 9999999);
    EXPECT_EQ(dst.bitrate_kbps, 6000u);
    EXPECT_EQ(dst.frame_duration_us, 16667u);
    EXPECT_EQ(dst.flags, protocol::flags::kKeyframe);
    EXPECT_EQ(dst.codec, protocol::VideoCodec::H264);
}

TEST(VideoHeader, WireSize)
{
    EXPECT_EQ(protocol::VideoHeader::kWireSize, 32u);
}

TEST(VideoHeader, ZeroValues)
{
    protocol::VideoHeader src { };
    uint8_t buf[protocol::VideoHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::VideoHeader::deserialize(buf);
    EXPECT_EQ(dst.width, 0u);
    EXPECT_EQ(dst.height, 0u);
    EXPECT_EQ(dst.bitrate_kbps, 0u);
    EXPECT_EQ(dst.frame_duration_us, 0u);
    EXPECT_EQ(dst.flags, 0u);
    EXPECT_EQ(dst.codec, protocol::VideoCodec::H264); // default codec
}

TEST(VideoHeader, MaxValues)
{
    protocol::VideoHeader src;
    src.width = UINT32_MAX;
    src.height = UINT32_MAX;
    src.sender_ts_us = INT64_MAX;
    src.bitrate_kbps = UINT32_MAX;
    src.frame_duration_us = UINT32_MAX;
    src.flags = UINT32_MAX;
    src.codec = protocol::VideoCodec::HEVC;

    uint8_t buf[protocol::VideoHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::VideoHeader::deserialize(buf);
    EXPECT_EQ(dst.width, UINT32_MAX);
    EXPECT_EQ(dst.height, UINT32_MAX);
    EXPECT_EQ(dst.bitrate_kbps, UINT32_MAX);
    EXPECT_EQ(dst.frame_duration_us, UINT32_MAX);
    EXPECT_EQ(dst.sender_ts_us, INT64_MAX);
    EXPECT_EQ(dst.flags, UINT32_MAX);
    EXPECT_EQ(dst.codec, protocol::VideoCodec::HEVC);
}

TEST(VideoHeader, CodecRoundtrip)
{
    for (auto codec : { protocol::VideoCodec::H264, protocol::VideoCodec::HEVC }) {
        protocol::VideoHeader src;
        src.width = 1280;
        src.height = 720;
        src.codec = codec;

        uint8_t buf[protocol::VideoHeader::kWireSize] { };
        src.serialize(buf);

        auto dst = protocol::VideoHeader::deserialize(buf);
        EXPECT_EQ(dst.codec, codec);
    }
}

TEST(VideoCodecEnum, ToString)
{
    EXPECT_STREQ(utils::to_string(protocol::VideoCodec::H264), "H264");
    EXPECT_STREQ(utils::to_string(protocol::VideoCodec::HEVC), "HEVC");
}

// ---- FrameHeader ----

TEST(FrameHeader, Roundtrip)
{
    protocol::FrameHeader src;
    src.frame_id = 1000;

    uint8_t buf[protocol::FrameHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::FrameHeader::deserialize(buf);
    EXPECT_EQ(dst.frame_id, 1000u);
}

TEST(FrameHeader, WireSize)
{
    EXPECT_EQ(protocol::FrameHeader::kWireSize, 8u);
}

TEST(FrameHeader, ZeroValues)
{
    protocol::FrameHeader src { };
    uint8_t buf[protocol::FrameHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::FrameHeader::deserialize(buf);
    EXPECT_EQ(dst.frame_id, 0u);
}

TEST(FrameHeader, MaxValues)
{
    protocol::FrameHeader src;
    src.frame_id = UINT64_MAX;

    uint8_t buf[protocol::FrameHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::FrameHeader::deserialize(buf);
    EXPECT_EQ(dst.frame_id, UINT64_MAX);
}

// ---- Headers don't overlap when packed adjacently ----

TEST(Protocol, AdjacentHeaders)
{
    uint8_t buf[protocol::AudioHeader::kWireSize + protocol::FrameHeader::kWireSize] { };

    protocol::AudioHeader ah;
    ah.seq = 77;
    ah.sender_ts_us = 555;
    ah.serialize(buf);

    protocol::FrameHeader fh;
    fh.frame_id = 99;
    fh.serialize(buf + protocol::AudioHeader::kWireSize);

    // Verify both still intact
    auto ah2 = protocol::AudioHeader::deserialize(buf);
    auto fh2 = protocol::FrameHeader::deserialize(
        buf + protocol::AudioHeader::kWireSize);
    EXPECT_EQ(ah2.seq, 77u);
    EXPECT_EQ(ah2.sender_ts_us, 555);
    EXPECT_EQ(fh2.frame_id, 99u);
}

// ---- Signaling JSON ----

TEST(SignalingProtocol, WelcomeRoundtrip)
{
    signaling::Welcome src;
    src.id = driscord::PeerId { "self" };
    src.peers.push_back(
        { driscord::PeerId { "peer-1" }, driscord::Username { "alice" } });
    src.streaming_peers.push_back(driscord::PeerId { "peer-2" });

    const auto parsed = signaling::parse(signaling::dump(src));
    ASSERT_TRUE(parsed);

    const auto* dst = std::get_if<signaling::Welcome>(&parsed.value());
    ASSERT_NE(dst, nullptr);
    EXPECT_EQ(dst->id.value, "self");
    ASSERT_EQ(dst->peers.size(), 1u);
    EXPECT_EQ(dst->peers[0].id.value, "peer-1");
    EXPECT_EQ(dst->peers[0].username.value, "alice");
    ASSERT_EQ(dst->streaming_peers.size(), 1u);
    EXPECT_EQ(dst->streaming_peers[0].value, "peer-2");
}

TEST(SignalingProtocol, CandidateRoundtrip)
{
    const auto parsed = signaling::parse(signaling::dump(
        signaling::Candidate { "candidate:1", "0" }));
    ASSERT_TRUE(parsed);

    const auto* candidate = std::get_if<signaling::Candidate>(&parsed.value());
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->candidate, "candidate:1");
    EXPECT_EQ(candidate->sdp_mid, "0");
}

TEST(SignalingProtocol, ControlMessageCanCarrySender)
{
    const auto parsed = signaling::parse(signaling::dump(
        signaling::StreamingStart { driscord::PeerId { "peer-1" } }));
    ASSERT_TRUE(parsed);

    const auto* start = std::get_if<signaling::StreamingStart>(&parsed.value());
    ASSERT_NE(start, nullptr);
    ASSERT_TRUE(start->from);
    EXPECT_EQ(start->from->value, "peer-1");
}

TEST(SignalingProtocol, RejectsUnknownMessageType)
{
    const auto parsed = signaling::parse(R"({"type":"bogus"})");
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), signaling::ParseError::UnknownType);
}

TEST(SignalingProtocol, RejectsMissingRequiredPayload)
{
    const auto parsed = signaling::parse(R"({"type":"candidate","candidate":"x"})");
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), signaling::ParseError::MissingField);
}

// ---- Relayed media framing ----

TEST(RelayedMedia, Roundtrip)
{
    const uint8_t payload[] = { 1, 2, 3, 4 };
    auto encoded = protocol::encode_relayed_media(
        driscord::PeerId { "peer-1" }, payload, sizeof(payload));
    ASSERT_TRUE(encoded);

    auto decoded = protocol::decode_relayed_media(
        reinterpret_cast<const uint8_t*>(encoded->data()), encoded->size());
    ASSERT_TRUE(decoded);

    EXPECT_EQ(decoded->sender_id.value, "peer-1");
    ASSERT_EQ(decoded->payload_len, sizeof(payload));
    EXPECT_EQ(std::memcmp(decoded->payload, payload, sizeof(payload)), 0);
}

TEST(RelayedMedia, RejectsOverlongSender)
{
    const uint8_t payload[] = { 1 };
    const std::string sender(protocol::kMaxRelayedMediaSenderLen + 1, 'x');

    auto encoded = protocol::encode_relayed_media(
        driscord::PeerId { sender }, payload, sizeof(payload));

    ASSERT_FALSE(encoded);
    EXPECT_EQ(encoded.error(), protocol::RelayedMediaError::SenderTooLong);
}

TEST(RelayedMedia, RejectsTruncatedPacket)
{
    const uint8_t packet[] = { 4, 'p', 'e' };

    auto decoded = protocol::decode_relayed_media(packet, sizeof(packet));

    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error(), protocol::RelayedMediaError::Truncated);
}

// ---- Media channel labels ----

TEST(MediaChannel, LabelRoundtrip)
{
    for (auto channel : {
             channel::MediaChannel::Audio,
             channel::MediaChannel::Video,
             channel::MediaChannel::ScreenAudio,
             channel::MediaChannel::Control,
         }) {
        auto parsed = channel::parse_label(channel::to_label(channel));
        ASSERT_TRUE(parsed);
        EXPECT_EQ(*parsed, channel);
    }
}

TEST(MediaChannel, RejectsUnknownLabel)
{
    EXPECT_FALSE(channel::parse_label("unknown"));
}

TEST(MediaChannel, WatcherGatingIsExplicit)
{
    EXPECT_FALSE(channel::is_watcher_gated(channel::MediaChannel::Audio));
    EXPECT_TRUE(channel::is_watcher_gated(channel::MediaChannel::Video));
    EXPECT_TRUE(channel::is_watcher_gated(channel::MediaChannel::ScreenAudio));
    EXPECT_FALSE(channel::is_watcher_gated(channel::MediaChannel::Control));
}
