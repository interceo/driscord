#pragma once

// Deterministic screen-content generators for the quality gates: text-like
// glyph runs, slide transitions, a static terminal and a video-in-window
// worst case. Screen share is its own content class — mostly static, high
// frequency, hard edges — and these produce it procedurally (no font or
// image assets) as BGRA buffers per frame index.

#include <cstdint>
#include <vector>

namespace test_util {

// Text-like page scrolled vertically by scroll_px_per_frame. Glyph runs are
// pseudo-random horizontal dashes on line grids: the spatial statistics of
// text (high-frequency edges the encoder must keep legible) without fonts.
[[nodiscard]] std::vector<uint8_t> scrolling_text_frame(int width,
    int height,
    size_t frame_index,
    int scroll_px_per_frame = 2);

// Slide-deck transition: full-frame colored blocks sliding horizontally,
// advancing one slide every slide_period frames.
[[nodiscard]] std::vector<uint8_t> sliding_blocks_frame(int width,
    int height,
    size_t frame_index,
    size_t slide_period = 90);

// Static terminal: fixed text-like grid, one blinking cursor cell. The
// near-zero temporal delta drives encoders into their static-content mode.
[[nodiscard]] std::vector<uint8_t> static_terminal_frame(int width,
    int height,
    size_t frame_index);

// Static desktop with an inner window of per-frame pseudo-noise — natural
// video inside screen-content encoder settings, the worst case.
[[nodiscard]] std::vector<uint8_t> noise_window_frame(int width,
    int height,
    size_t frame_index);

} // namespace test_util
