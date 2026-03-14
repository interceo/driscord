#pragma once

#include "ring_buffer.hpp"
#include "utils/byte_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

inline constexpr uint32_t kDefaultJitterMs = 80;

class AudioJitter {
public:
    explicit AudioJitter(size_t target_delay_ms = kDefaultJitterMs, int sample_rate = 48000)
        : ring_(sample_rate * kMaxBufferSeconds),
          target_samples_(target_delay_ms * sample_rate / 1000),
          sample_rate_(sample_rate) {}

    void push(const float* mono_pcm, size_t frames, uint16_t seq, uint32_t sender_ts = 0) {
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

        if (sender_ts != 0) {
            latest_sender_ts_.store(sender_ts, std::memory_order_relaxed);
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

            uint32_t latest = latest_sender_ts_.load(std::memory_order_relaxed);
            if (latest != 0) {
                uint64_t buf_samples = ring_.available_read();
                uint32_t buf_ms = static_cast<uint32_t>(buf_samples * 1000 / sample_rate_);
                playback_base_ts_.store(latest - buf_ms, std::memory_order_relaxed);
                playback_samples_.store(0, std::memory_order_relaxed);
                playback_local_ts_.store(drist::now_ms(), std::memory_order_relaxed);
            }
        }

        size_t buffered = ring_.available_read();
        if (buffered > target_samples_ * 2) {
            size_t to_discard = buffered - target_samples_;
            discard(to_discard);
            playback_samples_.fetch_add(to_discard, std::memory_order_relaxed);
        }

        size_t got = ring_.read(out, frames);
        if (got < frames) {
            std::memset(out + got, 0, (frames - got) * sizeof(float));
        }

        if (got > 0 && playback_base_ts_.load(std::memory_order_relaxed) != 0) {
            playback_samples_.fetch_add(got, std::memory_order_relaxed);
            playback_local_ts_.store(drist::now_ms(), std::memory_order_relaxed);
        }

        return got;
    }

    uint32_t current_playback_ts() const {
        uint32_t base = playback_base_ts_.load(std::memory_order_relaxed);
        if (base == 0) {
            return 0;
        }
        uint64_t samples = playback_samples_.load(std::memory_order_relaxed);
        uint32_t samples_ms = static_cast<uint32_t>(samples * 1000 / sample_rate_);
        uint32_t interp = drist::now_ms() - playback_local_ts_.load(std::memory_order_relaxed);
        return base + samples_ms + interp;
    }

    void reset() {
        ring_.clear();
        primed_ = false;
        first_packet_ = true;
        last_seq_ = 0;
        latest_sender_ts_.store(0, std::memory_order_relaxed);
        playback_base_ts_.store(0, std::memory_order_relaxed);
        playback_samples_.store(0, std::memory_order_relaxed);
        playback_local_ts_.store(0, std::memory_order_relaxed);
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

    std::atomic<uint32_t> latest_sender_ts_{0};
    std::atomic<uint32_t> playback_base_ts_{0};
    std::atomic<uint64_t> playback_samples_{0};
    std::atomic<uint32_t> playback_local_ts_{0};
};
