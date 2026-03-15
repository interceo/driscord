#pragma once

#include "log.hpp"
#include "time.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

// Slot-based ring jitter buffer keyed by sequence number.
//
// O(1) push/pop, zero heap allocations in hot path, cache-friendly.
//
// Initial delay: pop() returns nullopt until target_delay_ms have elapsed
// since the first push.
//
// Sender-timestamp pacing (pace_by_sender_ts = true): after priming, pop()
// returns a packet only when enough local time has elapsed to match the
// sender's capture timeline.
//
// Thread safety: push() and pop() may be called from different threads.

template <typename T>
class JitterBuffer {
public:
    using Clock = std::chrono::steady_clock;

    struct Packet {
        T data{};
        utils::WallTimestamp sender_ts{};
    };

    struct Stats {
        bool primed = false;
        size_t queue_size = 0;
        size_t buffered_ms = 0;
        uint64_t drop_count = 0;
        uint64_t miss_count = 0;
    };

    explicit JitterBuffer(int target_delay_ms, bool pace_by_sender_ts = false)
        : target_delay_ms_(std::max(1, target_delay_ms)), pace_by_sender_ts_(pace_by_sender_ts) {}

    void push(uint64_t seq, T data, utils::WallTimestamp sender_ts) {
        std::scoped_lock lk(mutex_);

        if (!first_push_time_) {
            first_push_time_ = Clock::now();
            next_seq_ = seq;
        }

        if (seq + kCapacity <= next_seq_) {
            ++drop_count_;
            return;
        }

        auto& slot = slots_[seq & kMask];
        if (slot.occupied) {
            if (slot.seq >= seq) {
                ++drop_count_;
                return;
            }
            --queue_size_;
            ++drop_count_;
        }

        slot.seq = seq;
        slot.pkt = Packet{std::move(data), sender_ts};
        slot.occupied = true;
        ++queue_size_;
    }

    std::optional<Packet> pop() {
        std::scoped_lock lk(mutex_);
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

        if (queue_size_ == 0) {
            ++miss_count_;
            return std::nullopt;
        }

        Slot* target = nullptr;
        size_t skip = 0;
        for (size_t i = 0; i < kCapacity; ++i) {
            auto& s = slots_[(next_seq_ + i) & kMask];
            if (s.occupied && s.seq == next_seq_ + i) {
                target = &s;
                skip = i;
                break;
            }
        }

        if (!target) {
            ++miss_count_;
            return std::nullopt;
        }

        if (skip > 0) {
            miss_count_ += skip;
            next_seq_ += skip;
        }

        if (pace_by_sender_ts_) {
            const auto& pkt_ts = target->pkt.sender_ts;
            if (pkt_ts.time_since_epoch().count() != 0) {
                if (!anchor_sender_ts_) {
                    anchor_sender_ts_ = pkt_ts;
                    playback_origin_ = now;
                } else {
                    const auto sender_delta = pkt_ts - *anchor_sender_ts_;
                    const auto local_delta = now - playback_origin_;
                    if (local_delta < std::chrono::duration_cast<Clock::duration>(sender_delta)) {
                        return std::nullopt;
                    }
                }
            }
        }

        auto pkt = std::move(target->pkt);
        target->occupied = false;
        target->pkt = {};
        --queue_size_;
        ++next_seq_;
        ++played_count_;

        if (played_count_ % 300 == 0) {
            const int64_t sender_ms = utils::WallToMs(pkt.sender_ts);
            const int64_t age_ms = sender_ms ? utils::WallElapsedMs(pkt.sender_ts) : -1;
            LOG_INFO()
                << "[jitter-pop] played=" << played_count_ << " queue=" << queue_size_ << " drops=" << drop_count_
                << " misses=" << miss_count_ << " sender_ts=" << sender_ms << " age_ms=" << age_ms;
        }

        return pkt;
    }

    bool primed() const {
        std::scoped_lock lk(mutex_);
        return primed_;
    }

    size_t queue_size() const {
        std::scoped_lock lk(mutex_);
        return queue_size_;
    }

    size_t buffered_ms() const {
        std::scoped_lock lk(mutex_);
        return buffered_ms_locked();
    }

    void reset() {
        std::scoped_lock lk(mutex_);
        for (auto& s : slots_) {
            s = {};
        }
        primed_ = false;
        first_push_time_.reset();
        next_seq_ = 0;
        queue_size_ = 0;
        anchor_sender_ts_.reset();
        playback_origin_ = {};
        played_count_ = 0;
        drop_count_ = 0;
        miss_count_ = 0;
    }

    Stats stats() const {
        std::scoped_lock lk(mutex_);
        return {
            primed_,
            queue_size_,
            buffered_ms_locked(),
            drop_count_,
            miss_count_,
        };
    }

private:
    static constexpr size_t kCapacity = 128;
    static constexpr size_t kMask = kCapacity - 1;
    static_assert((kCapacity & kMask) == 0, "kCapacity must be a power of 2");

    struct Slot {
        uint64_t seq = 0;
        bool occupied = false;
        Packet pkt{};
    };

    size_t buffered_ms_locked() const {
        utils::WallTimestamp min_ts = utils::WallTimestamp::max();
        utils::WallTimestamp max_ts = utils::WallTimestamp::min();
        bool found = false;
        for (const auto& s : slots_) {
            if (s.occupied && s.pkt.sender_ts.time_since_epoch().count() != 0) {
                if (s.pkt.sender_ts < min_ts) {
                    min_ts = s.pkt.sender_ts;
                }
                if (s.pkt.sender_ts > max_ts) {
                    max_ts = s.pkt.sender_ts;
                }
                found = true;
            }
        }
        if (!found) {
            return 0;
        }
        const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(max_ts - min_ts);
        return static_cast<size_t>(std::max(int64_t{0}, delta.count()));
    }

    mutable std::mutex mutex_;
    std::array<Slot, kCapacity> slots_{};

    bool primed_ = false;
    int target_delay_ms_;
    bool pace_by_sender_ts_;

    uint64_t next_seq_ = 0;
    size_t queue_size_ = 0;

    std::optional<Clock::time_point> first_push_time_;
    std::optional<utils::WallTimestamp> anchor_sender_ts_;
    Clock::time_point playback_origin_{};

    uint64_t played_count_ = 0;
    uint64_t drop_count_ = 0;
    uint64_t miss_count_ = 0;
};
