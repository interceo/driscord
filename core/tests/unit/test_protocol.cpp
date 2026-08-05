#include <gtest/gtest.h>

#include "utils/enum_strings.hpp"
#include "utils/protocol.hpp"

#include <cstring>

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
