#pragma once

#include <chrono>
#include <cstdint>

namespace utils {

using Timestamp = std::chrono::steady_clock::time_point;
using Duration  = std::chrono::steady_clock::duration;

inline Timestamp Now() {
    return std::chrono::steady_clock::now();
}

template <typename ToDuration = std::chrono::milliseconds>
inline int64_t ElapsedCount(const Timestamp from, const Timestamp to = Now()) {
    return std::chrono::duration_cast<ToDuration>(to - from).count();
}

inline int64_t ElapsedMs(const Timestamp from, const Timestamp to = Now()) {
    return ElapsedCount<std::chrono::milliseconds>(from, to);
}

inline Duration Elapsed(const Timestamp from, const Timestamp to = Now()) {
    return to - from;
}

using WallTimestamp = std::chrono::system_clock::time_point;

inline WallTimestamp WallNow() {
    return std::chrono::system_clock::now();
}

inline uint64_t WallToMs(const Timestamp ts) {
    return static_cast<
        uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch())
                      .count());
}

inline uint64_t WallToMs(const WallTimestamp ts) {
    return static_cast<
        uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch())
                      .count());
}

inline WallTimestamp WallFromMs(const uint64_t ms) {
    return WallTimestamp{std::chrono::milliseconds(ms)};
}

inline int64_t WallElapsedMs(const WallTimestamp from, const WallTimestamp to = WallNow()) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

inline uint64_t SinceEpochMs() {
    return WallToMs(WallNow());
}

} // namespace utils
