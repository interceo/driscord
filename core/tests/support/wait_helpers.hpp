#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <vector>

namespace test_util {

constexpr auto kDefaultTimeout = std::chrono::seconds(10);

class Waiter {
public:
    void signal()
    {
        {
            std::scoped_lock lk(m_);
            fired_ = true;
        }
        cv_.notify_all();
    }

    bool wait_for(std::chrono::milliseconds timeout = kDefaultTimeout)
    {
        std::unique_lock lk(m_);
        return cv_.wait_for(lk, timeout, [&] { return fired_; });
    }

    bool fired() const
    {
        std::scoped_lock lk(m_);
        return fired_;
    }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool fired_ { false };
};

template <typename T>
class EventCollector {
public:
    void push(T value)
    {
        {
            std::scoped_lock lk(m_);
            items_.push_back(std::move(value));
        }
        cv_.notify_all();
    }

    bool wait_for_count(size_t n,
        std::chrono::milliseconds timeout = kDefaultTimeout)
    {
        std::unique_lock lk(m_);
        return cv_.wait_for(lk, timeout, [&] { return items_.size() >= n; });
    }

    std::vector<T> snapshot() const
    {
        std::scoped_lock lk(m_);
        return items_;
    }

    size_t size() const
    {
        std::scoped_lock lk(m_);
        return items_.size();
    }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::vector<T> items_;
};

} // namespace test_util
