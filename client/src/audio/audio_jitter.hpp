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
        if (!seq_initialized_) {
            play_seq_ = seq;
            seq_initialized_ = true;
        }

        int16_t age = static_cast<int16_t>(seq - play_seq_);
        if (age < 0) {
            return;
        }
        if (static_cast<uint16_t>(age) >= kSlots) {
            return;
        }

        auto& slot = slots_[seq % kSlots];
        size_t n = std::min(frames, kFrameSize);
        std::memcpy(slot.pcm, mono_pcm, n * sizeof(float));
        slot.frames = n;
        slot.sender_ts = sender_ts;
        slot.filled = true;

        flush_ready();
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

    void re_anchor(uint32_t target_ts) {
        uint64_t buf_samples = ring_.available_read();
        uint32_t buf_ms = static_cast<uint32_t>(buf_samples * 1000 / sample_rate_);
        playback_base_ts_.store(target_ts - buf_ms, std::memory_order_relaxed);
        playback_samples_.store(0, std::memory_order_relaxed);
        playback_local_ts_.store(drist::now_ms(), std::memory_order_relaxed);
    }

    void reset() {
        ring_.clear();
        for (auto& s : slots_) {
            s.filled = false;
        }
        seq_initialized_ = false;
        play_seq_ = 0;
        last_flush_ts_ = 0;
        primed_ = false;
        latest_sender_ts_.store(0, std::memory_order_relaxed);
        playback_base_ts_.store(0, std::memory_order_relaxed);
        playback_samples_.store(0, std::memory_order_relaxed);
        playback_local_ts_.store(0, std::memory_order_relaxed);
    }

    size_t buffered_ms() const { return ring_.available_read() * 1000 / sample_rate_; }

private:
    void flush_ready() {
        uint32_t now = drist::now_ms();

        while (true) {
            auto& slot = slots_[play_seq_ % kSlots];
            if (slot.filled) {
                ring_.write(slot.pcm, slot.frames);
                if (slot.sender_ts != 0) {
                    latest_sender_ts_.store(slot.sender_ts, std::memory_order_relaxed);
                }
                slot.filled = false;
                ++play_seq_;
                last_flush_ts_ = now;
            } else {
                if (last_flush_ts_ != 0 && (now - last_flush_ts_) > kMaxWaitMs) {
                    fill_silence(kFrameSize);
                    ++play_seq_;
                    last_flush_ts_ = now;
                    continue;
                }
                break;
            }
        }
    }

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
    static constexpr size_t kFrameSize = 960;
    static constexpr size_t kSlots = 32;
    static constexpr uint32_t kMaxWaitMs = 60;

    struct Slot {
        float pcm[kFrameSize]{};
        size_t frames = 0;
        uint32_t sender_ts = 0;
        bool filled = false;
    };

    Slot slots_[kSlots]{};
    uint16_t play_seq_ = 0;
    bool seq_initialized_ = false;
    uint32_t last_flush_ts_ = 0;

    RingBuffer<float> ring_;
    size_t target_samples_;
    int sample_rate_;
    bool primed_ = false;

    std::atomic<uint32_t> latest_sender_ts_{0};
    std::atomic<uint32_t> playback_base_ts_{0};
    std::atomic<uint64_t> playback_samples_{0};
    std::atomic<uint32_t> playback_local_ts_{0};
};
