#pragma once

#include "utils/mono_clock.hpp"

#include <cstdint>

namespace test_util {

// Simulated monotonic clock. A scenario advances it explicitly, so a minute of
// media runs in milliseconds and produces the same numbers on every machine.
//
// Single-threaded by construction: the driver owns it and the receivers only
// read it from that same thread.
class SimClock final : public utils::TimeSource {
public:
    explicit SimClock(int64_t start_us = 0)
        : now_us_(start_us)
    {
    }

    int64_t now_us() const noexcept override { return now_us_; }

    void advance(int64_t delta_us) noexcept { now_us_ += delta_us; }
    void advance_to(int64_t t_us) noexcept
    {
        if (t_us > now_us_) {
            now_us_ = t_us;
        }
    }

private:
    int64_t now_us_;
};

} // namespace test_util
