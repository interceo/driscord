#pragma once

// Pixel-domain frame identity: a high-contrast cell grid stamped into the
// top-left corner of generated BGRA frames, decodable from the received I420
// luma after encode -> SFU -> decode (and a possible downscale). The marker
// carries only the frame index — the test that generated the frame knows the
// capture schedule, so capture time is looked up, never embedded. Stamping
// happens in the SOURCE frames: reference and received are compared
// marker-to-marker and the block never biases PSNR/SSIM.

#include "webrtc/google_webrtc_screen_session.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace test_util {

// Grid geometry at the reference (encoded) resolution. 16 px cells survive
// real-time quantization at 320x180 and one 2x downscale.
inline constexpr int kMarkerCellPx = 16;
inline constexpr int kMarkerCols = 8;
inline constexpr int kMarkerRows = 3;
inline constexpr int kMarkerMarginPx = 8;
inline constexpr int kMarkerIndexBits = 16;

struct DecodedMarker {
    uint32_t frame_index = 0;
};

// Stamps the marker into a BGRA buffer of (width x height). The frame index
// is Gray-coded so consecutive frames flip few cells, then checksummed.
void encode_frame_marker(std::span<uint8_t> bgra,
    int width,
    int height,
    int stride,
    uint32_t frame_index);

// Reads the marker back from decoded I420 luma. reference_width/height are
// the dimensions encode_frame_marker() ran at; the grid is rescaled by the
// actual decoded dimensions. Returns nullopt when the checksum fails.
[[nodiscard]] std::optional<DecodedMarker> decode_frame_marker(
    const driscord::media::DecodedVideoFrameView& frame,
    int reference_width,
    int reference_height);

} // namespace test_util
