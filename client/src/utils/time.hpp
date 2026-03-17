#pragma once

#include <chrono>
#include <cstdint>

namespace utils {

// Local monotonic clock — for measuring elapsed time within one process.
using Timestamp = std::chrono::steady_clock::time_point;
using Duration  = std::chrono::steady_clock::duration;

inline Timestamp Now() {
    return std::chrono::steady_clock::now();
}

template <typename ToDuration = std::chrono::milliseconds>
inline int64_t ElapsedCount(Timestamp from, Timestamp to = Now()) {
    return std::chrono::duration_cast<ToDuration>(to - from).count();
}

inline int64_t ElapsedMs(Timestamp from, Timestamp to = Now()) {
    return ElapsedCount<std::chrono::milliseconds>(from, to);
}

// Wall clock (UTC, system_clock) — timezone-agnostic, comparable across machines.
// Use this for any timestamps sent over the network.
using WallTimestamp = std::chrono::system_clock::time_point;

inline WallTimestamp WallNow() {
    return std::chrono::system_clock::now();
}

inline uint64_t WallToMs(WallTimestamp ts) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch()).count());
}

inline WallTimestamp WallFromMs(uint64_t ms) {
    return WallTimestamp{std::chrono::milliseconds(ms)};
}

inline int64_t WallElapsedMs(WallTimestamp from, WallTimestamp to = WallNow()) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

// Convenience: ms since Unix epoch for the current moment.
inline uint64_t SinceEpochMs() {
    return WallToMs(WallNow());
}

} // namespace utils
