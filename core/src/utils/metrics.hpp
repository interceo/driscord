#pragma once

#include <atomic>
#include <cstdint>

namespace utils {

// Monotonically increasing counter. All ops use relaxed atomics — safe to
// increment from any thread without additional synchronisation.
struct Counter {
    void inc(uint64_t n = 1) noexcept
    {
        val_.fetch_add(n, std::memory_order_relaxed);
    }

    uint64_t load() const noexcept
    {
        return val_.load(std::memory_order_relaxed);
    }

    void reset() noexcept { val_.store(0, std::memory_order_relaxed); }

private:
    std::atomic<uint64_t> val_ { 0 };
};

// Current-value gauge. All ops use relaxed atomics.
template <typename T>
struct Gauge {
    void set(T v) noexcept { val_.store(v, std::memory_order_relaxed); }
    T load() const noexcept { return val_.load(std::memory_order_relaxed); }

private:
    std::atomic<T> val_ { };
};

} // namespace utils
