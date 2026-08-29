#include "frame_marker.hpp"

#include <algorithm>
#include <array>

namespace test_util {

namespace {

    constexpr int kPayloadBits = kMarkerCols * kMarkerRows;
    constexpr int kChecksumBits = kPayloadBits - kMarkerIndexBits;
    static_assert(kChecksumBits == 8, "layout carries a one-byte checksum");

    constexpr uint32_t to_gray(uint32_t value)
    {
        return value ^ (value >> 1);
    }

    constexpr uint32_t from_gray(uint32_t gray)
    {
        uint32_t value = 0;
        for (; gray != 0; gray >>= 1) {
            value ^= gray;
        }
        return value;
    }

    constexpr uint8_t checksum(uint32_t index)
    {
        return static_cast<uint8_t>(
            (index & 0xFF) ^ ((index >> 8) & 0xFF) ^ 0xA5);
    }

    std::array<bool, kPayloadBits> payload_bits(uint32_t frame_index)
    {
        const uint32_t index = frame_index & 0xFFFFu;
        const uint32_t gray = to_gray(index);
        const uint8_t check = checksum(index);
        std::array<bool, kPayloadBits> bits { };
        for (int bit = 0; bit < kMarkerIndexBits; ++bit) {
            bits[static_cast<size_t>(bit)] = ((gray >> bit) & 1u) != 0;
        }
        for (int bit = 0; bit < kChecksumBits; ++bit) {
            bits[static_cast<size_t>(kMarkerIndexBits + bit)]
                = ((check >> bit) & 1u) != 0;
        }
        return bits;
    }

}

void encode_frame_marker(std::span<uint8_t> bgra,
    int width,
    int height,
    int stride,
    uint32_t frame_index)
{
    const auto bits = payload_bits(frame_index);
    const int block_width = kMarkerMarginPx * 2 + kMarkerCols * kMarkerCellPx;
    const int block_height = kMarkerMarginPx * 2 + kMarkerRows * kMarkerCellPx;
    if (width < block_width || height < block_height) {
        return;
    }
    for (int y = 0; y < block_height; ++y) {
        uint8_t* line = bgra.data() + static_cast<size_t>(y) * static_cast<size_t>(stride);
        for (int x = 0; x < block_width; ++x) {
            uint8_t* pixel = line + static_cast<size_t>(x) * 4;
            pixel[0] = 235;
            pixel[1] = 235;
            pixel[2] = 235;
            pixel[3] = 255;
        }
    }
    for (int row = 0; row < kMarkerRows; ++row) {
        for (int col = 0; col < kMarkerCols; ++col) {
            const bool set = bits[static_cast<size_t>(row * kMarkerCols + col)];
            const uint8_t value = set ? 16 : 235;
            const int origin_x = kMarkerMarginPx + col * kMarkerCellPx;
            const int origin_y = kMarkerMarginPx + row * kMarkerCellPx;
            for (int y = 0; y < kMarkerCellPx; ++y) {
                uint8_t* line = bgra.data()
                    + static_cast<size_t>(origin_y + y)
                        * static_cast<size_t>(stride);
                for (int x = 0; x < kMarkerCellPx; ++x) {
                    uint8_t* pixel
                        = line + static_cast<size_t>(origin_x + x) * 4;
                    pixel[0] = value;
                    pixel[1] = value;
                    pixel[2] = value;
                    pixel[3] = 255;
                }
            }
        }
    }
}

std::optional<DecodedMarker> decode_frame_marker(
    const driscord::media::DecodedVideoFrameView& frame,
    int reference_width,
    int reference_height)
{
    if (frame.y == nullptr || frame.width <= 0 || frame.height <= 0
        || reference_width <= 0 || reference_height <= 0) {
        return std::nullopt;
    }
    const double scale_x = static_cast<double>(frame.width) / reference_width;
    const double scale_y = static_cast<double>(frame.height) / reference_height;

    std::array<double, kMarkerCols * kMarkerRows> samples { };
    for (int row = 0; row < kMarkerRows; ++row) {
        for (int col = 0; col < kMarkerCols; ++col) {
            const double center_x = (kMarkerMarginPx
                                        + (col + 0.5) * kMarkerCellPx)
                * scale_x;
            const double center_y = (kMarkerMarginPx
                                        + (row + 0.5) * kMarkerCellPx)
                * scale_y;
            const int radius = std::max(
                1, static_cast<int>(kMarkerCellPx * scale_x / 4.0));
            double sum = 0.0;
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int y = static_cast<int>(center_y) + dy;
                if (y < 0 || y >= frame.height) {
                    continue;
                }
                const uint8_t* line = frame.y
                    + static_cast<size_t>(y)
                        * static_cast<size_t>(frame.y_stride);
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int x = static_cast<int>(center_x) + dx;
                    if (x < 0 || x >= frame.width) {
                        continue;
                    }
                    sum += line[x];
                    ++count;
                }
            }
            if (count == 0) {
                return std::nullopt;
            }
            samples[static_cast<size_t>(row * kMarkerCols + col)]
                = sum / count;
        }
    }
    const auto [darkest, brightest]
        = std::minmax_element(samples.begin(), samples.end());
    if (*brightest - *darkest < 32.0) {
        return std::nullopt;
    }
    const double threshold = (*darkest + *brightest) / 2.0;

    uint32_t gray = 0;
    uint8_t check = 0;
    for (int bit = 0; bit < kMarkerIndexBits; ++bit) {
        if (samples[static_cast<size_t>(bit)] < threshold) {
            gray |= 1u << bit;
        }
    }
    for (int bit = 0; bit < kPayloadBits - kMarkerIndexBits; ++bit) {
        if (samples[static_cast<size_t>(kMarkerIndexBits + bit)] < threshold) {
            check |= static_cast<uint8_t>(1u << bit);
        }
    }
    const uint32_t index = from_gray(gray);
    if (checksum(index) != check) {
        return std::nullopt;
    }
    return DecodedMarker { .frame_index = index };
}

}
