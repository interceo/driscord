#pragma once

#include "webrtc/google_webrtc_screen_session.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace test_util {

inline constexpr int kMarkerCellPx = 16;
inline constexpr int kMarkerCols = 8;
inline constexpr int kMarkerRows = 3;
inline constexpr int kMarkerMarginPx = 8;
inline constexpr int kMarkerIndexBits = 16;

struct DecodedMarker {
    uint32_t frame_index = 0;
};

void encode_frame_marker(std::span<uint8_t> bgra,
    int width,
    int height,
    int stride,
    uint32_t frame_index);

[[nodiscard]] std::optional<DecodedMarker> decode_frame_marker(
    const driscord::media::DecodedVideoFrameView& frame,
    int reference_width,
    int reference_height);

}
