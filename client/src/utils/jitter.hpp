#pragma once

#include "log.hpp"
#include "time.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

// Generic jitter buffer keyed by sequence number.
//
// Initial delay: pop() returns nullopt until target_delay_ms have elapsed since
// the first push.  After that packets are returned in sequence order.
//
// Sender-timestamp pacing (pace_by_sender_ts = true): after priming, pop()
// returns a packet only when enough local time has elapsed to match the
// sender's capture timeline.  This avoids drift from fixed-rate timers and
// handles variable frame rates naturally.
//
// Thread safety: push() and pop() may be called from different threads.
// T must be default-constructible and move-constructible.

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
        }

        if (map_.size() >= kMaxQueue) {
            const auto diff = map_.size() - kMaxQueue;
            map_.erase(map_.begin(), std::next(map_.begin(), diff));
            drop_count_ += diff;
        }

        map_.emplace(seq, Packet{std::move(data), sender_ts});
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

        auto it = map_.begin();
        if (it == map_.end()) {
            ++miss_count_;
            return std::nullopt;
        }

        if (pace_by_sender_ts_) {
            const auto pkt_ts = it->second.sender_ts;
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

        auto pkt = std::move(it->second);
        map_.erase(it);
        ++played_count_;

        if (played_count_ % 300 == 0) {
            const int64_t sender_ms = utils::WallToMs(pkt.sender_ts);
            const int64_t age_ms = sender_ms ? utils::WallElapsedMs(pkt.sender_ts) : -1;
            LOG_INFO()
                << "[jitter-pop] played=" << played_count_ << " queue=" << map_.size() << " drops=" << drop_count_
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
        return map_.size();
    }

    size_t buffered_ms() const {
        std::scoped_lock lk(mutex_);
        if (map_.size() < 2) {
            return 0;
        }
        const auto first_ts = map_.begin()->second.sender_ts;
        const auto last_ts = map_.rbegin()->second.sender_ts;
        const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(last_ts - first_ts);
        return static_cast<size_t>(std::max(int64_t{0}, delta.count()));
    }

    void reset() {
        std::scoped_lock lk(mutex_);

        map_.clear();
        primed_ = false;
        first_push_time_.reset();
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
            map_.size(),
            buffered_ms_locked(),
            drop_count_,
            miss_count_,
        };
    }

private:
    size_t buffered_ms_locked() const {
        if (map_.size() < 2) {
            return 0;
        }
        const auto first_ts = map_.begin()->second.sender_ts;
        const auto last_ts = map_.rbegin()->second.sender_ts;
        const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(last_ts - first_ts);
        return static_cast<size_t>(std::max(int64_t{0}, delta.count()));
    }

    static constexpr size_t kMaxQueue = 6400;

    mutable std::mutex mutex_;
    std::map<uint64_t, Packet> map_;

    bool primed_ = false;
    int target_delay_ms_;
    bool pace_by_sender_ts_;

    std::optional<Clock::time_point> first_push_time_;
    std::optional<utils::WallTimestamp> anchor_sender_ts_;
    Clock::time_point playback_origin_{};

    uint64_t played_count_ = 0;
    uint64_t drop_count_ = 0;
    uint64_t miss_count_ = 0;
};
