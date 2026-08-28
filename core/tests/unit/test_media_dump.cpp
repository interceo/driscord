#include "media_dump.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// The unit;media tier also runs as a Windows cross-build under Wine, where
// POSIX setenv does not exist.
void set_env(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (value[0] == '\0') {
        ::unsetenv(name);
    } else {
        ::setenv(name, value, 1);
    }
#endif
}

std::vector<uint8_t> read_all(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>() };
}

uint32_t u32_le(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset])
        | static_cast<uint32_t>(bytes[offset + 1]) << 8
        | static_cast<uint32_t>(bytes[offset + 2]) << 16
        | static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

uint16_t u16_le(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint16_t>(bytes[offset]
        | static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

class MediaDumpTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        dir_ = std::filesystem::temp_directory_path()
            / "driscord_media_dump_test";
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::filesystem::path dir_;
};

TEST_F(MediaDumpTest, WavRoundTripHasCanonicalPatchedHeader)
{
    const auto path = dir_ / "probe.wav";
    {
        test_util::WavWriter writer;
        ASSERT_TRUE(writer.open(path, 48'000, 2));
        const std::vector<int16_t> samples { 0, 1, -1, 32'767, -32'768, 42 };
        ASSERT_TRUE(writer.write(samples));
        writer.close();
    }
    const auto bytes = read_all(path);
    ASSERT_EQ(bytes.size(), 44u + 12u);
    EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 4), "RIFF");
    EXPECT_EQ(u32_le(bytes, 4), 36u + 12u);
    EXPECT_EQ(std::string(bytes.begin() + 8, bytes.begin() + 12), "WAVE");
    EXPECT_EQ(u16_le(bytes, 20), 1u); // PCM
    EXPECT_EQ(u16_le(bytes, 22), 2u); // channels
    EXPECT_EQ(u32_le(bytes, 24), 48'000u);
    EXPECT_EQ(u32_le(bytes, 28), 48'000u * 4u); // byte rate
    EXPECT_EQ(u16_le(bytes, 32), 4u); // block align
    EXPECT_EQ(u16_le(bytes, 34), 16u); // bits per sample
    EXPECT_EQ(u32_le(bytes, 40), 12u); // data chunk size
    // Little-endian payload: 32767 -> ff 7f, -32768 -> 00 80.
    EXPECT_EQ(bytes[44 + 6], 0xff);
    EXPECT_EQ(bytes[44 + 7], 0x7f);
    EXPECT_EQ(bytes[44 + 8], 0x00);
    EXPECT_EQ(bytes[44 + 9], 0x80);
}

TEST_F(MediaDumpTest, Y4mWriterHonorsPlaneStrides)
{
    constexpr int kWidth = 6;
    constexpr int kHeight = 4;
    constexpr int kLumaStride = 8; // padded rows must not leak into the file
    constexpr int kChromaStride = 5;
    std::vector<uint8_t> y(
        static_cast<size_t>(kLumaStride) * kHeight, 0xEE);
    std::vector<uint8_t> u(
        static_cast<size_t>(kChromaStride) * ((kHeight + 1) / 2), 0xEE);
    auto v = u;
    for (int row = 0; row < kHeight; ++row) {
        for (int col = 0; col < kWidth; ++col) {
            y[static_cast<size_t>(row) * kLumaStride + col]
                = static_cast<uint8_t>(row * 10 + col);
        }
    }
    for (int row = 0; row < (kHeight + 1) / 2; ++row) {
        for (int col = 0; col < (kWidth + 1) / 2; ++col) {
            u[static_cast<size_t>(row) * kChromaStride + col] = 0x40;
            v[static_cast<size_t>(row) * kChromaStride + col] = 0x80;
        }
    }

    const auto path = dir_ / "strided.y4m";
    {
        test_util::Y4mWriter writer;
        ASSERT_TRUE(writer.open(path, kWidth, kHeight));
        ASSERT_TRUE(writer.write_frame(y.data(), kLumaStride, u.data(),
            kChromaStride, v.data(), kChromaStride));
        EXPECT_EQ(writer.frames_written(), 1u);
    }
    const auto bytes = read_all(path);
    const std::string text(bytes.begin(), bytes.end());
    const auto frame_at = text.find("FRAME\n");
    ASSERT_NE(frame_at, std::string::npos);
    EXPECT_EQ(text.substr(0, 20), "YUV4MPEG2 W6 H4 F30:");
    const size_t payload = frame_at + 6;
    ASSERT_EQ(bytes.size(), payload + kWidth * kHeight + 2 * 3 * 2);
    // Tightly packed planes: no 0xEE padding byte may survive.
    for (size_t i = payload; i < bytes.size(); ++i) {
        EXPECT_NE(bytes[i], 0xEE) << "padding leaked at offset " << i;
    }
    // First luma row is 0..5, second starts at 10 — stride was honored.
    EXPECT_EQ(bytes[payload + 5], 5u);
    EXPECT_EQ(bytes[payload + 6], 10u);
}

TEST_F(MediaDumpTest, DumpDirComesFromEnvironment)
{
    // The variable is process-global; restore whatever the harness had set.
    const char* previous = std::getenv("DRISCORD_MEDIA_DUMP_DIR");
    const std::string saved = previous ? previous : "";
    set_env("DRISCORD_MEDIA_DUMP_DIR", "/tmp/driscord-dumps");
    ASSERT_TRUE(test_util::media_dump_dir().has_value());
    EXPECT_EQ(*test_util::media_dump_dir(),
        std::filesystem::path("/tmp/driscord-dumps"));
    set_env("DRISCORD_MEDIA_DUMP_DIR", "");
    EXPECT_FALSE(test_util::media_dump_dir().has_value());
    if (!saved.empty()) {
        set_env("DRISCORD_MEDIA_DUMP_DIR", saved.c_str());
    }
}

} // namespace
