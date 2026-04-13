#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Triple-buffered native RGBA frame slot for zero-copy JNI pass-through.
//
// The writer (decode thread) fills one buffer while the reader (UI thread)
// holds another. A third buffer ensures no contention: the writer can always
// find a free slot that is neither the latest published nor currently being read.
//
// Usage:
//   Writer:  ensure_capacity(w, h);
//            uint8_t* dst = acquire_write();
//            /* sws_scale / memcpy into dst */
//            publish(w, h);
//
//   Reader:  const uint8_t* src = acquire_read(w, h);
//            /* create Skia snapshot from src */
//            release_read();  // call once done (ok to defer to next update cycle)
class FrameSlot {
public:
    FrameSlot() = default;
    ~FrameSlot() { free_buffers(); }

    FrameSlot(const FrameSlot&) = delete;
    FrameSlot& operator=(const FrameSlot&) = delete;

    // (Re)allocate all three buffers if dimensions changed.
    void ensure_capacity(int w, int h)
    {
        const size_t needed = static_cast<size_t>(w) * h * 4;
        if (needed <= capacity_) {
            return;
        }
        free_buffers();
        capacity_ = needed;
        for (auto& buf : buffers_) {
            buf = static_cast<uint8_t*>(std::malloc(needed));
        }
    }

    // Returns a writable buffer that is neither the latest nor being read.
    uint8_t* acquire_write()
    {
        const int lat = latest_.load(std::memory_order_acquire);
        const int rd = reading_.load(std::memory_order_acquire);
        for (int i = 0; i < 3; ++i) {
            if (i != lat && i != rd) {
                write_idx_ = i;
                return buffers_[i];
            }
        }
        // Fallback (should not happen with 3 buffers and at most 2 held).
        write_idx_ = 0;
        return buffers_[0];
    }

    // Publish the most recently written buffer and record its dimensions.
    void publish(int w, int h)
    {
        w_ = w;
        h_ = h;
        latest_.store(write_idx_, std::memory_order_release);
    }

    // Acquire the latest published buffer for reading.  Returns nullptr if
    // nothing has been published yet.
    const uint8_t* acquire_read(int& w, int& h)
    {
        const int lat = latest_.load(std::memory_order_acquire);
        if (lat < 0) {
            return nullptr;
        }
        reading_.store(lat, std::memory_order_release);
        w = w_;
        h = h_;
        return buffers_[lat];
    }

    // Release the read lock so the buffer can be reused by the writer.
    void release_read()
    {
        reading_.store(-1, std::memory_order_release);
    }

    size_t capacity() const noexcept { return capacity_; }

private:
    void free_buffers()
    {
        for (auto& buf : buffers_) {
            std::free(buf);
            buf = nullptr;
        }
        capacity_ = 0;
        latest_.store(-1, std::memory_order_relaxed);
        reading_.store(-1, std::memory_order_relaxed);
    }

    uint8_t* buffers_[3] = {};
    std::atomic<int> latest_ { -1 };
    std::atomic<int> reading_ { -1 };
    int write_idx_ = 0;
    int w_ = 0;
    int h_ = 0;
    size_t capacity_ = 0;
};
