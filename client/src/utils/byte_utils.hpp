#pragma once

#include <chrono>
#include <cstdint>

namespace drist {

inline uint32_t now_ms() {
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    return static_cast<uint32_t>(ms);
}

inline void write_u16_le(uint8_t* dst, uint16_t v) {
    dst[0] = static_cast<uint8_t>(v);
    dst[1] = static_cast<uint8_t>(v >> 8);
}

inline uint16_t read_u16_le(const uint8_t* src) {
    return static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8);
}

inline void write_u32_le(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>(v);
    dst[1] = static_cast<uint8_t>(v >> 8);
    dst[2] = static_cast<uint8_t>(v >> 16);
    dst[3] = static_cast<uint8_t>(v >> 24);
}

inline uint32_t read_u32_le(const uint8_t* src) {
    return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

}  // namespace drist
