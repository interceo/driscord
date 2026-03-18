#pragma once

#include "log.hpp"
#include "slot_ring.hpp"
#include "time.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

template <typename T> class JitterBuffer {
public:
    using Clock = std::chrono::steady_clock;

    struct Packet {
        T data{};
        // Monotonic time when this packet was inserted locally.
        // Used for eviction — strictly increases per-packet, unaffected by
        // sender clock or network delay.
        Clock::time_point arrival{};
    };

    struct Stats {
        bool primed         = false;
        size_t queue_size   = 0;
        uint64_t drop_count = 0;
        uint64_t miss_count = 0;
    };

    explicit JitterBuffer(int target_delay_ms)
        : target_delay_ms_(std::max(1, target_delay_ms)) {}

    void push(uint64_t seq, T data) {
        std::scoped_lock lk(mutex_);

        const auto now = Clock::now();
        if (!first_push_time_) {
            first_push_time_ = now;
        }

        if (!ring_.push(seq, Packet{std::move(data), now})) {
            ++drop_count_;
        }
    }

    std::optional<Packet> pop() {
        std::unique_lock lk(mutex_, std::try_to_lock);
        if (!lk.owns_lock()) {
            return std::nullopt;
        }

        const auto now = Clock::now();

        if (!first_push_time_) {
            return std::nullopt;
        }

        if (!primed_) {
            if (now - *first_push_time_ < std::chrono::milliseconds(target_delay_ms_)) {
                return std::nullopt;
            }
            primed_ = true;
        }

        if (ring_.empty()) {
            return std::nullopt;
        }

        auto peek = ring_.peek_next();
        if (!peek) {
            return std::nullopt;
        }

        if (peek->skipped > 0) {
            ring_.advance_seq();
            ++miss_count_; // real sequence gap — lost packet
            return std::nullopt;
        }

        auto result = ring_.consume_peeked(0);
        ++played_count_;

        return std::move(result.data);
    }

    // Evict packets from the front of the sequence queue that have been
    // sitting in the buffer for longer than max_delay_ms. Iterates in
    // sequence order and stops at the first packet that is still fresh,
    // so a recently-arrived out-of-order packet at the front will not be
    // evicted even if older frames sit behind it.
    size_t evict_old(int max_delay_ms) {
        std::scoped_lock lk(mutex_);

        const auto cutoff = Clock::now() - std::chrono::milliseconds(max_delay_ms);
        size_t dropped    = 0;

        while (true) {
            auto peek = ring_.peek_next();
            if (!peek) {
                break;
            }
            // Stop at the first packet that is still within the delay window.
            if (peek->data->arrival >= cutoff) {
                break;
            }
            ring_.consume_peeked(peek->skipped);
            ++drop_count_;
            ++dropped;
        }

        return dropped;
    }

    // Age in ms of the oldest packet waiting to be consumed (front of sequence queue).
    // Returns -1 if the buffer is empty. Used for A/V sync.
    int64_t front_age_ms() const {
        std::scoped_lock lk(mutex_);
        auto peek = ring_.peek_next();
        if (!peek) {
            return -1;
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   Clock::now() - peek->data->arrival)
            .count();
    }

    bool primed() const {
        std::scoped_lock lk(mutex_);
        return primed_;
    }

    size_t queue_size() const {
        std::scoped_lock lk(mutex_);
        return ring_.size();
    }

    void reset() {
        std::scoped_lock lk(mutex_);
        ring_.reset();
        primed_ = false;
        first_push_time_.reset();
        played_count_ = 0;
        drop_count_   = 0;
        miss_count_   = 0;
    }

    Stats stats() const {
        std::scoped_lock lk(mutex_);
        return {primed_, ring_.size(), drop_count_, miss_count_};
    }

private:
    mutable std::mutex mutex_;
    SlotRing<Packet> ring_;

    bool primed_ = false;
    int target_delay_ms_;

    std::optional<Clock::time_point> first_push_time_;

    uint64_t played_count_ = 0;
    uint64_t drop_count_   = 0;
    uint64_t miss_count_   = 0;
};
