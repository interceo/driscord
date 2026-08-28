#include "media_dump.hpp"

#include <array>
#include <cstdlib>
#include <cstring>

namespace test_util {

std::optional<std::filesystem::path> media_dump_dir()
{
    const char* value = std::getenv("DRISCORD_MEDIA_DUMP_DIR");
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path { value };
}

namespace {

    bool write_plane(std::FILE* file,
        const uint8_t* plane,
        int stride,
        int width,
        int height)
    {
        for (int row = 0; row < height; ++row) {
            const uint8_t* line
                = plane + static_cast<size_t>(row) * static_cast<size_t>(stride);
            if (std::fwrite(line, 1, static_cast<size_t>(width), file)
                != static_cast<size_t>(width)) {
                return false;
            }
        }
        return true;
    }

    void put_u32_le(uint8_t* out, uint32_t value)
    {
        out[0] = static_cast<uint8_t>(value);
        out[1] = static_cast<uint8_t>(value >> 8);
        out[2] = static_cast<uint8_t>(value >> 16);
        out[3] = static_cast<uint8_t>(value >> 24);
    }

    void put_u16_le(uint8_t* out, uint16_t value)
    {
        out[0] = static_cast<uint8_t>(value);
        out[1] = static_cast<uint8_t>(value >> 8);
    }

} // namespace

bool Y4mWriter::open(const std::filesystem::path& path,
    int width,
    int height,
    int fps_numerator,
    int fps_denominator)
{
    close();
    if (width <= 0 || height <= 0 || fps_numerator <= 0
        || fps_denominator <= 0) {
        return false;
    }
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    file_ = std::fopen(path.string().c_str(), "wb");
    if (file_ == nullptr) {
        return false;
    }
    width_ = width;
    height_ = height;
    frames_ = 0;
    // C420mpeg2 (chroma sited between lines) matches what the decode path
    // hands out; the exact siting tag does not affect plane-wise metrics.
    if (std::fprintf(file_, "YUV4MPEG2 W%d H%d F%d:%d Ip A1:1 C420mpeg2\n",
            width, height, fps_numerator, fps_denominator)
        < 0) {
        close();
        return false;
    }
    return true;
}

bool Y4mWriter::write_frame(const uint8_t* y,
    int y_stride,
    const uint8_t* u,
    int u_stride,
    const uint8_t* v,
    int v_stride)
{
    if (file_ == nullptr || y == nullptr || u == nullptr || v == nullptr) {
        return false;
    }
    const int chroma_width = (width_ + 1) / 2;
    const int chroma_height = (height_ + 1) / 2;
    if (std::fputs("FRAME\n", file_) < 0
        || !write_plane(file_, y, y_stride, width_, height_)
        || !write_plane(file_, u, u_stride, chroma_width, chroma_height)
        || !write_plane(file_, v, v_stride, chroma_width, chroma_height)) {
        close();
        return false;
    }
    ++frames_;
    return true;
}

void Y4mWriter::close()
{
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

bool WavWriter::open(const std::filesystem::path& path,
    int sample_rate_hz,
    uint16_t channels)
{
    close();
    if (sample_rate_hz <= 0 || channels == 0) {
        return false;
    }
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    file_ = std::fopen(path.string().c_str(), "wb");
    if (file_ == nullptr) {
        return false;
    }
    data_bytes_ = 0;

    const auto rate = static_cast<uint32_t>(sample_rate_hz);
    const uint16_t block_align = static_cast<uint16_t>(channels * 2u);
    std::array<uint8_t, 44> header { };
    std::memcpy(header.data(), "RIFF", 4);
    put_u32_le(header.data() + 4, 36); // patched on close
    std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    put_u32_le(header.data() + 16, 16); // PCM fmt chunk size
    put_u16_le(header.data() + 20, 1); // PCM
    put_u16_le(header.data() + 22, channels);
    put_u32_le(header.data() + 24, rate);
    put_u32_le(header.data() + 28, rate * block_align);
    put_u16_le(header.data() + 32, block_align);
    put_u16_le(header.data() + 34, 16); // bits per sample
    std::memcpy(header.data() + 36, "data", 4);
    put_u32_le(header.data() + 40, 0); // patched on close
    if (std::fwrite(header.data(), 1, header.size(), file_) != header.size()) {
        close();
        return false;
    }
    return true;
}

bool WavWriter::write(std::span<const int16_t> interleaved_samples)
{
    if (file_ == nullptr) {
        return false;
    }
    // Sample bytes go out little-endian; every target this test tier runs on
    // (Linux x86_64 and the Wine x64 gate) is little-endian, so the in-memory
    // layout is already the wire layout.
    const size_t bytes = interleaved_samples.size() * sizeof(int16_t);
    if (std::fwrite(interleaved_samples.data(), 1, bytes, file_) != bytes) {
        close();
        return false;
    }
    data_bytes_ += bytes;
    return true;
}

void WavWriter::close()
{
    if (file_ == nullptr) {
        return;
    }
    const auto data = static_cast<uint32_t>(data_bytes_);
    std::array<uint8_t, 4> size_field { };
    put_u32_le(size_field.data(), 36 + data);
    if (std::fseek(file_, 4, SEEK_SET) == 0) {
        (void)std::fwrite(size_field.data(), 1, 4, file_);
    }
    put_u32_le(size_field.data(), data);
    if (std::fseek(file_, 40, SEEK_SET) == 0) {
        (void)std::fwrite(size_field.data(), 1, 4, file_);
    }
    std::fclose(file_);
    file_ = nullptr;
}

} // namespace test_util
