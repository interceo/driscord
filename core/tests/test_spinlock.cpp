#include <gtest/gtest.h>

#include "utils/spinlock.hpp"

#include <atomic>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// 1. Basic lock/unlock
TEST(SpinLock, BasicLockUnlock) {
    utils::SpinLock sl;
    sl.lock();
    sl.unlock();
}

// 2. try_lock succeeds when unlocked
TEST(SpinLock, TryLockSucceeds) {
    utils::SpinLock sl;
    EXPECT_TRUE(sl.try_lock());
    sl.unlock();
}

// 3. try_lock fails when locked
TEST(SpinLock, TryLockFailsWhenLocked) {
    utils::SpinLock sl;
    sl.lock();

    std::atomic<bool> got_lock{false};
    std::thread t([&] { got_lock = sl.try_lock(); });
    t.join();

    EXPECT_FALSE(got_lock);
    sl.unlock();
}

// 4. scoped_lock works (Lockable concept)
TEST(SpinLock, ScopedLock) {
    utils::SpinLock sl;
    { std::scoped_lock lk(sl); }
    EXPECT_TRUE(sl.try_lock());
    sl.unlock();
}

// 5. unique_lock with try_to_lock works
TEST(SpinLock, UniqueLockTryToLock) {
    utils::SpinLock sl;
    {
        std::unique_lock lk(sl, std::try_to_lock);
        EXPECT_TRUE(lk.owns_lock());
    }
    // After scope, should be unlocked
    EXPECT_TRUE(sl.try_lock());
    sl.unlock();
}

// 6. Mutual exclusion — concurrent counter increment
TEST(SpinLock, MutualExclusion) {
    utils::SpinLock sl;
    int counter           = 0;
    constexpr int N       = 100'000;
    constexpr int THREADS = 4;

    auto worker = [&] {
        for (int i = 0; i < N; ++i) {
            std::scoped_lock lk(sl);
            ++counter;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(counter, N * THREADS);
}

// 7. lock() blocks until unlock
TEST(SpinLock, LockBlocks) {
    utils::SpinLock sl;
    sl.lock();

    std::atomic<bool> entered{false};
    std::thread t([&] {
        sl.lock();
        entered = true;
        sl.unlock();
    });

    std::this_thread::sleep_for(5ms);
    EXPECT_FALSE(entered);

    sl.unlock();
    t.join();
    EXPECT_TRUE(entered);
}

// 8. Stress — many short lock/unlock cycles from multiple threads
TEST(SpinLock, Stress) {
    utils::SpinLock sl;
    constexpr int THREADS = 8;
    constexpr int OPS     = 50'000;
    std::atomic<int> sum{0};

    auto worker = [&] {
        for (int i = 0; i < OPS; ++i) {
            std::scoped_lock lk(sl);
            sum.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(sum.load(), THREADS * OPS);
}
