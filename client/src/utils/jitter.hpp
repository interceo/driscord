#pragma once

#include "log.hpp"
#include "time.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

// Generic jitter buffer keyed by sequence number.
//
// Ordering is by seq number only — sender_ts is stored alongside the payload
// and exposed via current_playback_ts() for A/V synchronization purposes only.
//
// Thread safety: push() and pop() may be called from different threads.
//
// T must be default-constructible (default value is used as silence/placeholder
// when a packet is missing but the sequence must advance).

template <typename T>
class JitterBuffer {
public:
    struct Packet {
        T data{};
        utils::WallTimestamp sender_ts{};
    };

    struct Stats {
        bool primed = false;
        size_t queue_size = 0;
        size_t buffered_ms = 0;
        uint64_t push_count = 0;
        uint64_t drop_count = 0;
        uint64_t miss_count = 0;
        utils::WallTimestamp playback_ts{};
    };

    // packet_duration_ms: how long one packet represents in milliseconds.
    explicit JitterBuffer(int target_delay_ms, int packet_duration_ms)
        : packet_ms_(std::max(1, packet_duration_ms)),
          target_(std::max(1, target_delay_ms / std::max(1, packet_duration_ms))),
          max_queue_(target_ * 4) {}

    void push(uint64_t seq, T data, utils::WallTimestamp sender_ts = {}) {
        std::scoped_lock lk(mutex_);

        if (primed_ && seq < next_seq_) {
            ++drop_count_;
            return;
        }

        // Drop oldest if overfull.
        if (map_.size() >= max_queue_) {
            const auto diff = map_.size() - max_queue_;
            map_.erase(map_.begin(), std::next(map_.begin(), diff));
            drop_count_ += diff;
        }

        map_.emplace(seq, Packet{std::move(data), sender_ts});
    }

    // Returns the next packet in sequence order.
    // Returns a default-constructed Packet (silence) if the packet is missing.
    // Returns std::nullopt if not yet primed (or after re-priming on underrun).
    std::optional<Packet> pop() {
        std::scoped_lock lk(mutex_);

        if (!primed_) {
            if (map_.size() < target_) {
                return std::nullopt;
            }
            primed_ = true;
            next_seq_ = map_.begin()->first;
            consecutive_misses_ = 0;
            anchor_locked();
            LOG_INFO() << "[jitter] PRIMED: q=" << map_.size() << " next_seq=" << next_seq_ << " target=" << target_;
        }

        // Drain overrun: burst arrivals can push the buffer far above target.
        // Skip oldest packets to keep latency near target.
        if (map_.size() > target_ * 2) {
            const size_t to_discard = map_.size() - target_;
            for (size_t i = 0; i < to_discard && !map_.empty(); ++i) {
                map_.erase(map_.begin());
                ++drop_count_;
            }
            if (!map_.empty()) {
                next_seq_ = map_.begin()->first;
            }
            LOG_INFO()
                << "[jitter] DRAIN: discarded=" << to_discard << " new_next_seq=" << next_seq_ << " q=" << map_.size();
        }

        // Evict stale entries that fell behind the playback head.
        while (!map_.empty() && map_.begin()->first < next_seq_) {
            map_.erase(map_.begin());
            ++drop_count_;
        }

        Packet pkt{};
        auto it = map_.find(next_seq_);
        if (it != map_.end()) {
            pkt = std::move(it->second);
            map_.erase(it);
            consecutive_misses_ = 0;
        } else {
            ++miss_count_;
            ++consecutive_misses_;
            if (miss_count_ % 50 == 0) {
                LOG_INFO()
                    << "[jitter] miss#" << miss_count_ << " seq=" << next_seq_ << " q=" << map_.size()
                    << " streak=" << consecutive_misses_;
            }
            // Re-prime after extended silence so playback re-syncs when
            // the sender resumes instead of drifting on forever.
            if (consecutive_misses_ >= kRePrimeThreshold) {
                primed_ = false;
                consecutive_misses_ = 0;
                playback_base_ms_.store(0, std::memory_order_relaxed);
                LOG_INFO()
                    << "[jitter] RE-PRIME: extended gap at seq=" << next_seq_ << " threshold=" << kRePrimeThreshold;
                return std::nullopt;
            }
        }

        ++next_seq_;
        advance_locked();
        return pkt;
    }

    bool primed() const {
        std::scoped_lock lk(mutex_);
        return primed_;
    }

    size_t buffered_ms() const {
        std::scoped_lock lk(mutex_);
        return map_.size() * static_cast<size_t>(packet_ms_);
    }

    size_t queue_size() const {
        std::scoped_lock lk(mutex_);
        return map_.size();
    }

    // Returns estimated wall-clock position of the playback head.
    // Thread-safe (reads only atomics).
    utils::WallTimestamp current_playback_ts() const {
        const uint64_t base = playback_base_ms_.load();
        if (base == 0) {
            return {};
        }
        const uint64_t played_ms = played_count_.load() * static_cast<uint64_t>(packet_ms_);
        const uint64_t interp = utils::SinceEpochMs() - playback_local_ms_.load();
        return utils::WallFromMs(base + played_ms + interp);
    }

    // Re-anchor the playback clock to target_ts (called on A/V resync).
    void re_anchor(utils::WallTimestamp target_ts) {
        std::scoped_lock lk(mutex_);
        const uint64_t buf_ms = map_.size() * static_cast<uint64_t>(packet_ms_);
        playback_base_ms_.store(utils::WallToMs(target_ts) - buf_ms);
        played_count_.store(0);
        playback_local_ms_.store(utils::SinceEpochMs());
    }

    void reset() {
        std::scoped_lock lk(mutex_);
        map_.clear();
        primed_ = false;
        next_seq_ = 0;
        push_count_ = 0;
        drop_count_ = 0;
        miss_count_ = 0;
        consecutive_misses_ = 0;
        played_count_.store(0);
        playback_base_ms_.store(0);
        playback_local_ms_.store(0);
    }

    Stats stats() const {
        std::scoped_lock lk(mutex_);
        return {
            .primed = primed_,
            .queue_size = map_.size(),
            .buffered_ms = map_.size() * static_cast<size_t>(packet_ms_),
            .push_count = push_count_,
            .drop_count = drop_count_,
            .miss_count = miss_count_,
            .playback_ts = current_playback_ts(),
        };
    }

private:
    void anchor_locked() {
        if (map_.empty()) {
            return;
        }
        const auto& first = map_.begin()->second;
        if (first.sender_ts != utils::WallTimestamp{}) {
            playback_base_ms_.store(utils::WallToMs(first.sender_ts));
        } else {
            playback_base_ms_.store(utils::SinceEpochMs());
        }
        played_count_.store(0);
        playback_local_ms_.store(utils::SinceEpochMs());
    }

    void advance_locked() {
        played_count_.fetch_add(1);
        playback_local_ms_.store(utils::SinceEpochMs());
    }

    mutable std::mutex mutex_;
    std::map<uint64_t, Packet> map_;

    int packet_ms_;
    size_t target_;
    size_t max_queue_;

    bool primed_ = false;
    uint64_t next_seq_ = 0;

    // 10 consecutive misses = 200ms silence at 20ms/frame → re-prime.
    static constexpr uint64_t kRePrimeThreshold = 10;

    uint64_t push_count_ = 0;
    uint64_t drop_count_ = 0;
    uint64_t miss_count_ = 0;
    uint64_t consecutive_misses_ = 0;

    std::atomic<uint64_t> played_count_{0};
    std::atomic<uint64_t> playback_base_ms_{0};
    std::atomic<uint64_t> playback_local_ms_{0};
};
