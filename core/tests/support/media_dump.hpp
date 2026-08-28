#pragma once

// Opt-in raw media dumps for offline analysis with stock tooling (ffmpeg
// psnr/ssim/libvmaf, rtc_tools/frame_analyzer, ViSQOL): Y4M for I420 video,
// PCM16 WAV for audio. DRISCORD_MEDIA_DUMP_DIR selects the output directory;
// while it is unset every writer stays inert, so the dump hooks cost the CI
// gate nothing. scripts/media_metrics_crosscheck.sh consumes the files.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>

namespace test_util {

// Directory from DRISCORD_MEDIA_DUMP_DIR, or nullopt when dumping is off.
[[nodiscard]] std::optional<std::filesystem::path> media_dump_dir();

// YUV4MPEG2 stream of fixed-size I420 frames. All writes are best-effort:
// a failed open leaves the writer inert instead of failing the test.
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

    // Strided I420 planes with the dimensions passed to open().
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

// Canonical 44-byte-header PCM16 WAV; sizes are patched on close, so an
// unclosed file is still readable by tools that trust the data chunk length
// of zero less than the actual file size (ffmpeg does).
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

} // namespace test_util
