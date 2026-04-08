#pragma once

#include <cstdint>

#include "time.hpp"

namespace utils {

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

inline void write_u64_le(uint8_t* dst, uint64_t v) {
    dst[0] = static_cast<uint8_t>(v);
    dst[1] = static_cast<uint8_t>(v >> 8);
    dst[2] = static_cast<uint8_t>(v >> 16);
    dst[3] = static_cast<uint8_t>(v >> 24);
    dst[4] = static_cast<uint8_t>(v >> 32);
    dst[5] = static_cast<uint8_t>(v >> 40);
    dst[6] = static_cast<uint8_t>(v >> 48);
    dst[7] = static_cast<uint8_t>(v >> 56);
}

inline uint64_t read_u64_le(const uint8_t* src) {
    return static_cast<uint64_t>(src[0]) | (static_cast<uint64_t>(src[1]) << 8) |
           (static_cast<uint64_t>(src[2]) << 16) | (static_cast<uint64_t>(src[3]) << 24) |
           (static_cast<uint64_t>(src[4]) << 32) | (static_cast<uint64_t>(src[5]) << 40) |
           (static_cast<uint64_t>(src[6]) << 48) | (static_cast<uint64_t>(src[7]) << 56);
}

} // namespace utils
