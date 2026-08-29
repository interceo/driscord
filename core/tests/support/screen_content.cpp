#include "screen_content.hpp"

#include <algorithm>
#include <cstddef>

namespace test_util {

using std::size_t;

namespace {

    uint64_t hash64(uint64_t value)
    {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    void fill(std::vector<uint8_t>& bgra,
        size_t offset,
        uint8_t b,
        uint8_t g,
        uint8_t r)
    {
        bgra[offset] = b;
        bgra[offset + 1] = g;
        bgra[offset + 2] = r;
        bgra[offset + 3] = 255;
    }

    void render_text_row(std::vector<uint8_t>& bgra,
        int width,
        int screen_row,
        int document_row,
        uint64_t salt)
    {
        constexpr int kLineHeight = 10;
        constexpr int kGlyphHeight = 7;
        const int line = document_row / kLineHeight;
        const int within = document_row % kLineHeight;
        const size_t base
            = static_cast<size_t>(screen_row) * static_cast<size_t>(width) * 4;
        const bool glyph_band = within >= 1 && within <= kGlyphHeight;
        int x = 8;
        uint64_t state = hash64(salt * 1'000'003 + static_cast<uint64_t>(line));
        while (x < width - 8) {
            const int dash = 3 + static_cast<int>(hash64(state) % 9);
            const int gap = 2 + static_cast<int>(hash64(state + 1) % 4);
            state = hash64(state + 2);
            const bool word_end = (hash64(state) % 7) == 0;
            for (int dx = 0; dx < dash && x + dx < width - 8; ++dx) {
                const size_t offset
                    = base + static_cast<size_t>(x + dx) * 4;
                if (glyph_band) {
                    fill(bgra, offset, 24, 24, 24);
                }
            }
            x += dash + gap + (word_end ? 6 : 0);
        }
    }

}

std::vector<uint8_t> scrolling_text_frame(int width,
    int height,
    size_t frame_index,
    int scroll_px_per_frame)
{
    std::vector<uint8_t> bgra(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            fill(bgra,
                (static_cast<size_t>(y) * static_cast<size_t>(width)
                    + static_cast<size_t>(x))
                    * 4,
                248, 248, 246);
        }
    }
    const int scroll = static_cast<int>(frame_index)
        * scroll_px_per_frame;
    for (int y = 0; y < height; ++y) {
        render_text_row(bgra, width, y, y + scroll, 1);
    }
    return bgra;
}

std::vector<uint8_t> sliding_blocks_frame(int width,
    int height,
    size_t frame_index,
    size_t slide_period)
{
    std::vector<uint8_t> bgra(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    const size_t slide = frame_index / slide_period;
    const size_t within = frame_index % slide_period;
    constexpr size_t kTransitionFrames = 30;
    const double progress = within >= kTransitionFrames
        ? 1.0
        : static_cast<double>(within) / kTransitionFrames;
    const int shift
        = static_cast<int>((1.0 - progress) * static_cast<double>(width));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t offset
                = (static_cast<size_t>(y) * static_cast<size_t>(width)
                      + static_cast<size_t>(x))
                * 4;
            const int content_x = x - shift;
            const uint64_t cell = hash64(slide * 512
                + static_cast<uint64_t>(std::max(content_x, 0) / 40) * 16
                + static_cast<uint64_t>(y / 40));
            if (content_x < 0) {
                fill(bgra, offset, 250, 250, 250);
            } else {
                fill(bgra, offset, static_cast<uint8_t>(64 + cell % 160),
                    static_cast<uint8_t>(64 + (cell >> 8) % 160),
                    static_cast<uint8_t>(64 + (cell >> 16) % 160));
            }
        }
    }
    return bgra;
}

std::vector<uint8_t> static_terminal_frame(int width,
    int height,
    size_t frame_index)
{
    std::vector<uint8_t> bgra(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            fill(bgra,
                (static_cast<size_t>(y) * static_cast<size_t>(width)
                    + static_cast<size_t>(x))
                    * 4,
                24, 18, 12);
        }
    }
    for (int y = 8; y < height - 8; ++y) {
        constexpr int kLineHeight = 10;
        const int within = y % kLineHeight;
        if (within < 1 || within > 7) {
            continue;
        }
        uint64_t state
            = hash64(7'777 + static_cast<uint64_t>(y / kLineHeight));
        int x = 8;
        while (x < width - 8) {
            const int dash = 3 + static_cast<int>(hash64(state) % 9);
            const int gap = 2 + static_cast<int>(hash64(state + 1) % 4);
            state = hash64(state + 2);
            for (int dx = 0; dx < dash && x + dx < width - 8; ++dx) {
                fill(bgra,
                    (static_cast<size_t>(y) * static_cast<size_t>(width)
                        + static_cast<size_t>(x + dx))
                        * 4,
                    120, 220, 140);
            }
            x += dash + gap;
        }
    }
    if ((frame_index / 15) % 2 == 0) {
        const int cursor_y = height - 20;
        for (int y = cursor_y; y < cursor_y + 9 && y < height; ++y) {
            for (int x = 10; x < 18 && x < width; ++x) {
                fill(bgra,
                    (static_cast<size_t>(y) * static_cast<size_t>(width)
                        + static_cast<size_t>(x))
                        * 4,
                    120, 220, 140);
            }
        }
    }
    return bgra;
}

std::vector<uint8_t> noise_window_frame(int width,
    int height,
    size_t frame_index)
{
    auto bgra = scrolling_text_frame(width, height, 0, 0);
    const int window_x = width / 4;
    const int window_y = height / 4;
    const int window_w = width / 2;
    const int window_h = height / 2;
    for (int y = 0; y < window_h; ++y) {
        for (int x = 0; x < window_w; ++x) {
            const uint64_t noise = hash64(frame_index * 1'000'000'007ull
                + static_cast<uint64_t>(y) * 8'191
                + static_cast<uint64_t>(x));
            fill(bgra,
                (static_cast<size_t>(window_y + y)
                        * static_cast<size_t>(width)
                    + static_cast<size_t>(window_x + x))
                    * 4,
                static_cast<uint8_t>(noise), static_cast<uint8_t>(noise >> 8),
                static_cast<uint8_t>(noise >> 16));
        }
    }
    return bgra;
}

}
