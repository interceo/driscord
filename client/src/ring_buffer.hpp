#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

// Lock-free single-producer single-consumer ring buffer for audio samples.
// Producer: network thread writing decoded PCM.
// Consumer: audio playback callback reading PCM.
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : buf_(capacity + 1), capacity_(capacity + 1) {}

    size_t available_read() const {
        size_t w = write_pos_.load(std::memory_order_acquire);
        size_t r = read_pos_.load(std::memory_order_relaxed);
        return (w >= r) ? (w - r) : (capacity_ - r + w);
    }

    size_t available_write() const {
        size_t w = write_pos_.load(std::memory_order_relaxed);
        size_t r = read_pos_.load(std::memory_order_acquire);
        return (r > w) ? (r - w - 1) : (capacity_ - w + r - 1);
    }

    size_t write(const T* data, size_t count) {
        size_t avail = available_write();
        if (count > avail) count = avail;
        if (count == 0) return 0;

        size_t w = write_pos_.load(std::memory_order_relaxed);
        size_t first = std::min(count, capacity_ - w);
        std::memcpy(&buf_[w], data, first * sizeof(T));
        if (count > first) {
            std::memcpy(&buf_[0], data + first, (count - first) * sizeof(T));
        }
        write_pos_.store((w + count) % capacity_, std::memory_order_release);
        return count;
    }

    size_t read(T* data, size_t count) {
        size_t avail = available_read();
        if (count > avail) count = avail;
        if (count == 0) return 0;

        size_t r = read_pos_.load(std::memory_order_relaxed);
        size_t first = std::min(count, capacity_ - r);
        std::memcpy(data, &buf_[r], first * sizeof(T));
        if (count > first) {
            std::memcpy(data + first, &buf_[0], (count - first) * sizeof(T));
        }
        read_pos_.store((r + count) % capacity_, std::memory_order_release);
        return count;
    }

    void clear() {
        read_pos_.store(0, std::memory_order_relaxed);
        write_pos_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<T> buf_;
    size_t capacity_;
    std::atomic<size_t> read_pos_{0};
    std::atomic<size_t> write_pos_{0};
};
