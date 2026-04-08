#pragma once

#include <atomic>

namespace utils {

class SpinLock {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            flag_.wait(true, std::memory_order_relaxed);
        }
    }

    bool try_lock() noexcept {
        return !flag_.test(std::memory_order_relaxed) &&
               !flag_.test_and_set(std::memory_order_acquire);
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
        flag_.notify_one();
    }

private:
    std::atomic_flag flag_{};
};

} // namespace utils
