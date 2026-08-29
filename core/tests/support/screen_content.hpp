#pragma once

#include <cstdint>
#include <vector>

namespace test_util {

[[nodiscard]] std::vector<uint8_t> scrolling_text_frame(int width,
    int height,
    size_t frame_index,
    int scroll_px_per_frame = 2);

[[nodiscard]] std::vector<uint8_t> sliding_blocks_frame(int width,
    int height,
    size_t frame_index,
    size_t slide_period = 90);

[[nodiscard]] std::vector<uint8_t> static_terminal_frame(int width,
    int height,
    size_t frame_index);

[[nodiscard]] std::vector<uint8_t> noise_window_frame(int width,
    int height,
    size_t frame_index);

}
