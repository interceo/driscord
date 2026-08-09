#pragma once

#include <cstddef>
#include <deque>
#include <utility>

namespace utils {

enum class QueuePushResult {
    Accepted,
    DroppedFull,
    Closed,
};

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity)
        : capacity_(capacity)
    {
    }

    QueuePushResult push_back(T value)
    {
        if (closed_) {
            return QueuePushResult::Closed;
        }
        if (items_.size() >= capacity_) {
            return QueuePushResult::DroppedFull;
        }
        items_.push_back(std::move(value));
        return QueuePushResult::Accepted;
    }

    T& front() { return items_.front(); }
    const T& front() const { return items_.front(); }

    void pop_front() { items_.pop_front(); }

    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }

    void close() { closed_ = true; }
    bool closed() const { return closed_; }

private:
    size_t capacity_;
    bool closed_ = false;
    std::deque<T> items_;
};

} // namespace utils
