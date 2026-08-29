
#include "test/rtp_file_reader.h"
#include "test/rtp_file_writer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> make_rtp_packet(uint16_t sequence,
    uint32_t timestamp,
    uint32_t ssrc,
    uint8_t payload_type,
    size_t payload_length)
{
    std::vector<uint8_t> packet(12 + payload_length);
    packet[0] = 0x80;
    packet[1] = payload_type;
    packet[2] = static_cast<uint8_t>(sequence >> 8);
    packet[3] = static_cast<uint8_t>(sequence & 0xff);
    packet[4] = static_cast<uint8_t>(timestamp >> 24);
    packet[5] = static_cast<uint8_t>(timestamp >> 16);
    packet[6] = static_cast<uint8_t>(timestamp >> 8);
    packet[7] = static_cast<uint8_t>(timestamp & 0xff);
    packet[8] = static_cast<uint8_t>(ssrc >> 24);
    packet[9] = static_cast<uint8_t>(ssrc >> 16);
    packet[10] = static_cast<uint8_t>(ssrc >> 8);
    packet[11] = static_cast<uint8_t>(ssrc & 0xff);
    for (size_t i = 0; i < payload_length; ++i) {
        packet[12 + i] = static_cast<uint8_t>((sequence + i) & 0xff);
    }
    return packet;
}

TEST(RtpDumpRoundTrip, WriteThenReadPreservesBytesAndTimestamps)
{
    const std::string path = ::testing::TempDir() + "driscord_roundtrip.rtpdump";
    constexpr uint32_t kSsrc = 0x11223344;
    constexpr size_t kPacketCount = 50;

    {
        std::unique_ptr<webrtc::test::RtpFileWriter> writer(
            webrtc::test::RtpFileWriter::Create(
                webrtc::test::RtpFileWriter::kRtpDump, path));
        ASSERT_NE(writer, nullptr);
        for (size_t i = 0; i < kPacketCount; ++i) {
            const auto bytes = make_rtp_packet(
                static_cast<uint16_t>(1000 + i),
                static_cast<uint32_t>(i * 960), kSsrc, 111, 40 + (i % 7));
            webrtc::test::RtpPacket packet;
            ASSERT_LE(bytes.size(), sizeof(packet.data));
            std::memcpy(packet.data, bytes.data(), bytes.size());
            packet.length = bytes.size();
            packet.original_length = bytes.size();
            packet.time_ms = static_cast<uint32_t>(i * 20);
            ASSERT_TRUE(writer->WritePacket(&packet));
        }
    }

    std::unique_ptr<webrtc::test::RtpFileReader> reader(
        webrtc::test::RtpFileReader::Create(
            webrtc::test::RtpFileReader::kRtpDump, path));
    ASSERT_NE(reader, nullptr);
    size_t read_count = 0;
    webrtc::test::RtpPacket packet;
    while (reader->NextPacket(&packet)) {
        const auto expected = make_rtp_packet(
            static_cast<uint16_t>(1000 + read_count),
            static_cast<uint32_t>(read_count * 960), kSsrc, 111,
            40 + (read_count % 7));
        ASSERT_EQ(packet.length, expected.size());
        ASSERT_EQ(packet.original_length, expected.size());
        EXPECT_EQ(std::memcmp(packet.data, expected.data(), expected.size()), 0)
            << "payload mismatch at packet " << read_count;
        EXPECT_EQ(packet.time_ms, read_count * 20);
        ++read_count;
    }
    EXPECT_EQ(read_count, kPacketCount);

    reader.reset();
    std::filesystem::remove(path);
}

TEST(RtpDumpRoundTrip, SsrcFilterIsIgnoredByTheRtpDumpReader)
{
    const std::string path = ::testing::TempDir() + "driscord_filter.rtpdump";
    {
        std::unique_ptr<webrtc::test::RtpFileWriter> writer(
            webrtc::test::RtpFileWriter::Create(
                webrtc::test::RtpFileWriter::kRtpDump, path));
        ASSERT_NE(writer, nullptr);
        for (uint32_t ssrc : { 0xAAAAAAAAu, 0xBBBBBBBBu }) {
            for (uint16_t i = 0; i < 10; ++i) {
                const auto bytes = make_rtp_packet(i, i * 960u, ssrc, 96, 24);
                webrtc::test::RtpPacket packet;
                std::memcpy(packet.data, bytes.data(), bytes.size());
                packet.length = bytes.size();
                packet.original_length = bytes.size();
                packet.time_ms = i;
                ASSERT_TRUE(writer->WritePacket(&packet));
            }
        }
    }

    std::unique_ptr<webrtc::test::RtpFileReader> reader(
        webrtc::test::RtpFileReader::Create(
            webrtc::test::RtpFileReader::kRtpDump, path, { 0xBBBBBBBBu }));
    ASSERT_NE(reader, nullptr);
    size_t read_count = 0;
    webrtc::test::RtpPacket packet;
    while (reader->NextPacket(&packet)) {
        ++read_count;
    }
    EXPECT_EQ(read_count, 20u);

    reader.reset();
    std::filesystem::remove(path);
}

TEST(RtpDumpRoundTrip, GarbageFileIsRejectedAtOpen)
{
    const std::string path = ::testing::TempDir() + "driscord_garbage.rtpdump";
    {
        std::ofstream out(path, std::ios::binary);
        out << "this is not an rtpdump capture at all";
    }

    std::unique_ptr<webrtc::test::RtpFileReader> reader(
        webrtc::test::RtpFileReader::Create(
            webrtc::test::RtpFileReader::kRtpDump, path));
    EXPECT_EQ(reader, nullptr);

    std::filesystem::remove(path);
}

}
