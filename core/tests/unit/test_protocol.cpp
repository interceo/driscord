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

// ---- ChunkHeader ----

TEST(ChunkHeader, Roundtrip)
{
    protocol::ChunkHeader src;
    src.frame_id = 1000;
    src.chunk_idx = 3;
    src.total_chunks = 10;

    uint8_t buf[protocol::ChunkHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::ChunkHeader::deserialize(buf);
    EXPECT_EQ(dst.frame_id, 1000u);
    EXPECT_EQ(dst.chunk_idx, 3u);
    EXPECT_EQ(dst.total_chunks, 10u);
}

TEST(ChunkHeader, WireSize)
{
    EXPECT_EQ(protocol::ChunkHeader::kWireSize, 12u);
}

TEST(ChunkHeader, ZeroValues)
{
    protocol::ChunkHeader src { };
    uint8_t buf[protocol::ChunkHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::ChunkHeader::deserialize(buf);
    EXPECT_EQ(dst.frame_id, 0u);
    EXPECT_EQ(dst.chunk_idx, 0u);
    EXPECT_EQ(dst.total_chunks, 0u);
}

TEST(ChunkHeader, MaxValues)
{
    protocol::ChunkHeader src;
    src.frame_id = UINT64_MAX;
    src.chunk_idx = UINT16_MAX;
    src.total_chunks = UINT16_MAX;

    uint8_t buf[protocol::ChunkHeader::kWireSize] { };
    src.serialize(buf);

    auto dst = protocol::ChunkHeader::deserialize(buf);
    EXPECT_EQ(dst.frame_id, UINT64_MAX);
    EXPECT_EQ(dst.chunk_idx, UINT16_MAX);
    EXPECT_EQ(dst.total_chunks, UINT16_MAX);
}

// ---- Headers don't overlap when packed adjacently ----

TEST(Protocol, AdjacentHeaders)
{
    uint8_t buf[protocol::AudioHeader::kWireSize + protocol::ChunkHeader::kWireSize] { };

    protocol::AudioHeader ah;
    ah.seq = 77;
    ah.sender_ts_us = 555;
    ah.serialize(buf);

    protocol::ChunkHeader ch;
    ch.frame_id = 99;
    ch.chunk_idx = 2;
    ch.total_chunks = 5;
    ch.serialize(buf + protocol::AudioHeader::kWireSize);

    // Verify both still intact
    auto ah2 = protocol::AudioHeader::deserialize(buf);
    auto ch2 = protocol::ChunkHeader::deserialize(
        buf + protocol::AudioHeader::kWireSize);
    EXPECT_EQ(ah2.seq, 77u);
    EXPECT_EQ(ch2.frame_id, 99u);
    EXPECT_EQ(ch2.total_chunks, 5u);
}
