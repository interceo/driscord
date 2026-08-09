#pragma once

#include <cstdint>

#include "time.hpp"

namespace utils {

class MonoClock {
public:
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

class TimeSource {
public:
    virtual ~TimeSource() = default;
    virtual int64_t now_us() const noexcept = 0;
};

inline const TimeSource& system_time_source() noexcept
{
    static const struct SystemTimeSource final : TimeSource {
        int64_t now_us() const noexcept override { return MonoClock::now_us(); }
    } kSource;
    return kSource;
}

} // namespace utils
