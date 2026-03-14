#pragma once

#include "ring_buffer.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

inline constexpr uint32_t kJitterTargetMs = 40;

class AudioJitter {
public:
    explicit AudioJitter(size_t target_delay_ms = kJitterTargetMs, int sample_rate = 48000)
        : ring_(sample_rate * kMaxBufferSeconds),
          target_samples_(target_delay_ms * sample_rate / 1000),
          sample_rate_(sample_rate) {}

    void push(const float* mono_pcm, size_t frames, uint16_t seq) {
        if (first_packet_) {
            first_packet_ = false;
            last_seq_ = seq;
        } else {
            uint16_t expected = last_seq_ + 1;
            uint16_t gap = seq - expected;
            if (gap > 0 && gap < kMaxGap) {
                size_t silence = static_cast<size_t>(gap) * kFrameSize;
                fill_silence(silence);
            }
            last_seq_ = seq;
        }

        ring_.write(mono_pcm, frames);
    }

    size_t pop(float* out, size_t frames) {
        if (!primed_) {
            if (ring_.available_read() < target_samples_) {
                std::memset(out, 0, frames * sizeof(float));
                return 0;
            }
            primed_ = true;
        }

        size_t buffered = ring_.available_read();
        if (buffered > target_samples_ * 2) {
            discard(buffered - target_samples_);
        }

        size_t got = ring_.read(out, frames);
        if (got < frames) {
            std::memset(out + got, 0, (frames - got) * sizeof(float));
        }
        return got;
    }

    void reset() {
        ring_.clear();
        primed_ = false;
        first_packet_ = true;
        last_seq_ = 0;
    }

    size_t buffered_ms() const { return ring_.available_read() * 1000 / sample_rate_; }

private:
    void fill_silence(size_t samples) {
        constexpr size_t kChunk = 256;
        float zeros[kChunk]{};
        while (samples > 0) {
            size_t n = std::min(samples, kChunk);
            ring_.write(zeros, n);
            samples -= n;
        }
    }

    void discard(size_t samples) {
        constexpr size_t kChunk = 256;
        float trash[kChunk];
        while (samples > 0) {
            size_t n = std::min(samples, kChunk);
            ring_.read(trash, n);
            samples -= n;
        }
    }

    static constexpr int kMaxBufferSeconds = 1;
    static constexpr size_t kFrameSize = 960;  // 20ms @ 48kHz
    static constexpr uint16_t kMaxGap = 10;    // ignore gaps > 10 packets (likely seq wrap)

    RingBuffer<float> ring_;
    size_t target_samples_;
    int sample_rate_;
    bool primed_ = false;
    bool first_packet_ = true;
    uint16_t last_seq_ = 0;
};
