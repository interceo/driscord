#pragma once

#include <cstdint>

#include "time.hpp"

namespace utils {

// Monotonic timeline stamped onto every outgoing media packet.
//
// Every stream a process sends — voice, screen video, screen audio — reads its
// timestamps from this one clock, so a receiver can compare an audio packet's
// timestamp against a video frame's directly and place both on a single
// playout timeline. A wall clock cannot serve that role: an NTP step mid-call
// shifts one stream relative to the other with no way to tell the jump apart
// from a network delay change.
class MonoClock {
public:
    // Microseconds since the first call in this process.
    static int64_t now_us() noexcept
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            Now() - epoch())
            .count();
    }

private:
    static Timestamp epoch() noexcept
    {
        static const Timestamp kEpoch = Now();
        return kEpoch;
    }
};

} // namespace utils
