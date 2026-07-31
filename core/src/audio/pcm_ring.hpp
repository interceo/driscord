#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

// Decoded-sample ring sitting between the Opus decoder and the output device.
//
// Without it the playout is pinned to the assumption that one device callback
// wants exactly one Opus frame. ALSA and PulseAudio routinely hand over a
// period other than the one that was requested, and under that assumption the
// tail of a decoded frame is silently dropped or the callback is left partly
// unfilled — a steady, permanent loss of audio.
//
// This holds only the impedance mismatch between the two, a few frames at
// most. The playout delay itself lives upstream as undecoded packets, which is
// what lets the buffer be resized without discarding decoded audio.
//
// Single-threaded by construction: it is filled and drained from inside the
// same audio callback.
class PcmRing {
public:
    explicit PcmRing(const size_t capacity)
        : buf_(capacity)
    {
    }

    size_t size() const noexcept { return count_; }
    size_t capacity() const noexcept { return buf_.size(); }
    size_t space() const noexcept { return buf_.size() - count_; }
    bool empty() const noexcept { return count_ == 0; }

    // Writes as much as fits; returns how much was taken.
    size_t write(const float* src, size_t n) noexcept
    {
        n = std::min(n, space());
        const size_t first = std::min(n, buf_.size() - head_);
        std::memcpy(buf_.data() + head_, src, first * sizeof(float));
        if (n > first) {
            std::memcpy(buf_.data(), src + first, (n - first) * sizeof(float));
        }
        head_ = (head_ + n) % buf_.size();
        count_ += n;
        return n;
    }

    size_t read(float* dst, size_t n) noexcept
    {
        n = std::min(n, count_);
        const size_t first = std::min(n, buf_.size() - tail_);
        std::memcpy(dst, buf_.data() + tail_, first * sizeof(float));
        if (n > first) {
            std::memcpy(dst + first, buf_.data(), (n - first) * sizeof(float));
        }
        tail_ = (tail_ + n) % buf_.size();
        count_ -= n;
        return n;
    }

    void clear() noexcept
    {
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

private:
    std::vector<float> buf_;
    size_t head_ = 0; // write position
    size_t tail_ = 0; // read position
    size_t count_ = 0;
};
