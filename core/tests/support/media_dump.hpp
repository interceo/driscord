#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>

namespace test_util {

[[nodiscard]] std::optional<std::filesystem::path> media_dump_dir();

class Y4mWriter {
public:
    Y4mWriter() = default;
    ~Y4mWriter() { close(); }

    Y4mWriter(const Y4mWriter&) = delete;
    Y4mWriter& operator=(const Y4mWriter&) = delete;

    bool open(const std::filesystem::path& path,
        int width,
        int height,
        int fps_numerator = 30,
        int fps_denominator = 1);

    bool write_frame(const uint8_t* y,
        int y_stride,
        const uint8_t* u,
        int u_stride,
        const uint8_t* v,
        int v_stride);

    void close();

    [[nodiscard]] bool is_open() const noexcept { return file_ != nullptr; }
    [[nodiscard]] size_t frames_written() const noexcept { return frames_; }

private:
    std::FILE* file_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    size_t frames_ = 0;
};

class WavWriter {
public:
    WavWriter() = default;
    ~WavWriter() { close(); }

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool open(const std::filesystem::path& path,
        int sample_rate_hz,
        uint16_t channels);

    bool write(std::span<const int16_t> interleaved_samples);

    void close();

    [[nodiscard]] bool is_open() const noexcept { return file_ != nullptr; }

private:
    std::FILE* file_ = nullptr;
    uint64_t data_bytes_ = 0;
};

}
