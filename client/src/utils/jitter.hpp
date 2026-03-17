#pragma once

#include "log.hpp"
#include "slot_ring.hpp"
#include "time.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

// Thread-safe jitter buffer built on top of SlotRing.
//
// push() — uses scoped_lock (network thread can afford a brief wait).
// pop()  — uses try_lock so the audio callback NEVER blocks.
//
// Initial delay: pop() returns nullopt until target_delay_ms have elapsed
// since the first push.
//
// Sender-timestamp pacing (pace_by_sender_ts = true): after priming, pop()
// returns a packet only when enough local time has elapsed to match the
// sender's capture timeline.
//
// max_excess_ms (pace_by_sender_ts only): if sender_delta exceeds local
// elapsed time by more than this threshold the anchor is reset and the frame
// is released immediately.  Prevents unbounded latency growth from clock skew
// between sender and receiver.  0 = disabled.
//
// max_queue_size: hard cap on in-flight packets.  When the ring exceeds this
// depth the oldest packets are dropped before pacing, so slow consumers never
// cause unbounded memory growth.  0 = disabled.

template <typename T> class JitterBuffer {
public:
    using Clock = std::chrono::steady_clock;

    struct Packet {
        T data{};
        utils::WallTimestamp sender_ts{};
    };

    struct Stats {
        bool primed         = false;
        size_t queue_size   = 0;
        size_t buffered_ms  = 0;
        uint64_t drop_count = 0;
        uint64_t miss_count = 0;
    };

    explicit JitterBuffer(
        int target_delay_ms,
        bool pace_by_sender_ts = false,
        int max_excess_ms      = 0,
        size_t max_queue_size  = 0
    )
        : target_delay_ms_(std::max(1, target_delay_ms))
        , pace_by_sender_ts_(pace_by_sender_ts)
        , max_excess_ms_(max_excess_ms)
        , max_queue_size_(max_queue_size) {}

    // ── push (network thread) ────────────────────────────────────────

    void push(uint64_t seq, T data, utils::WallTimestamp sender_ts) {
        std::scoped_lock lk(mutex_);

        if (!first_push_time_) {
            first_push_time_ = Clock::now();
        }

        if (!ring_.push(seq, Packet{std::move(data), sender_ts})) {
            ++drop_count_;
        }
    }

    // ── pop (audio callback / render thread) ─────────────────────────
    // Uses try_lock: returns nullopt immediately if the mutex is held
    // by push(), guaranteeing the real-time audio thread never stalls.

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

        // Hard queue cap: drop oldest packets until within limit.
        if (max_queue_size_ > 0) {
            while (ring_.size() > max_queue_size_) {
                if (auto discarded = ring_.pop()) {
                    ++drop_count_;
                    // Reset anchor so playback resumes from the newest frame
                    // rather than trying to replay the skipped timeline.
                    anchor_sender_ts_.reset();
                } else {
                    break;
                }
            }
        }

        if (ring_.empty()) {
            ++miss_count_;
            return std::nullopt;
        }

        auto peek = ring_.peek_next();
        if (!peek) {
            ++miss_count_;
            return std::nullopt;
        }

        // Gap detected: insert silence for one missing frame instead of jumping
        // ahead all at once, which would compress the timeline and sound like speedup.
        if (peek->skipped > 0) {
            ring_.advance_seq();
            ++miss_count_;
            return std::nullopt;
        }

        if (pace_by_sender_ts_) {
            const auto& pkt_ts = peek->data->sender_ts;
            if (pkt_ts.time_since_epoch().count() != 0) {
                if (!anchor_sender_ts_) {
                    anchor_sender_ts_ = pkt_ts;
                    playback_origin_  = now;
                } else {
                    const auto sender_delta = pkt_ts - *anchor_sender_ts_;
                    const auto local_delta  = now - playback_origin_;

                    // Clock-skew guard: if sender is more than max_excess_ms ahead
                    // of our local clock, reset the anchor and release the frame now.
                    const int64_t
                        sender_ms = std::chrono::duration_cast<std::chrono::milliseconds>(sender_delta).count();
                    const int64_t local_ms = std::chrono::duration_cast<std::chrono::milliseconds>(local_delta).count();
                    if (max_excess_ms_ > 0 && (sender_ms - local_ms) > max_excess_ms_) {
                        LOG_WARNING()
                            << "[jitter] clock skew " << (sender_ms - local_ms) << "ms > " << max_excess_ms_
                            << "ms — resetting anchor";
                        anchor_sender_ts_ = pkt_ts;
                        playback_origin_  = now;
                        // Fall through and release the frame immediately.
                    } else if (local_delta < std::chrono::duration_cast<Clock::duration>(sender_delta)) {
                        return std::nullopt;
                    }
                }
            }
        }

        auto result = ring_.consume_peeked(0);
        ++played_count_;

        if (played_count_ % 300 == 0) {
            const int64_t sender_ms = utils::WallToMs(result.data.sender_ts);
            const int64_t age_ms    = sender_ms ? utils::WallElapsedMs(result.data.sender_ts) : -1;
            LOG_INFO()
                << "[jitter-pop] played=" << played_count_ << " queue=" << ring_.size() << " drops=" << drop_count_
                << " misses=" << miss_count_ << " sender_ts=" << sender_ms << " age_ms=" << age_ms;
        }

        return std::move(result.data);
    }

    // Discard all queued packets whose sender_ts is strictly before ts_ms.
    // Callable from any thread (uses scoped_lock). Resets the pacing anchor
    // so playback resumes from the first frame after the drain.
    size_t drain_before(uint64_t ts_ms) {
        std::scoped_lock lk(mutex_);
        size_t dropped = 0;
        while (true) {
            auto peek = ring_.peek_next();
            if (!peek) {
                break;
            }
            if (utils::WallToMs(peek->data->sender_ts) >= ts_ms) {
                break;
            }
            ring_.consume_peeked(peek->skipped);
            ++drop_count_;
            ++dropped;
        }
        if (dropped > 0) {
            anchor_sender_ts_.reset();
        }
        return dropped;
    }

    // ── queries ──────────────────────────────────────────────────────

    bool primed() const {
        std::scoped_lock lk(mutex_);
        return primed_;
    }

    size_t queue_size() const {
        std::scoped_lock lk(mutex_);
        return ring_.size();
    }

    size_t buffered_ms() const {
        std::scoped_lock lk(mutex_);
        return buffered_ms_locked();
    }

    void reset() {
        std::scoped_lock lk(mutex_);
        ring_.reset();
        primed_ = false;
        first_push_time_.reset();
        anchor_sender_ts_.reset();
        playback_origin_ = {};
        played_count_    = 0;
        drop_count_      = 0;
        miss_count_      = 0;
    }

    Stats stats() const {
        std::scoped_lock lk(mutex_);
        return {
            primed_,
            ring_.size(),
            buffered_ms_locked(),
            drop_count_,
            miss_count_,
        };
    }

private:
    size_t buffered_ms_locked() const {
        utils::WallTimestamp min_ts = utils::WallTimestamp::max();
        utils::WallTimestamp max_ts = utils::WallTimestamp::min();
        bool found                  = false;
        ring_.for_each_occupied([&](const auto& slot) {
            const auto& ts = slot.data.sender_ts;
            if (ts.time_since_epoch().count() != 0) {
                if (ts < min_ts) {
                    min_ts = ts;
                }
                if (ts > max_ts) {
                    max_ts = ts;
                }
                found = true;
            }
        });
        if (!found) {
            return size_t{0};
        }
        const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(max_ts - min_ts);
        return static_cast<size_t>(std::max(int64_t{0}, delta.count()));
    }

    mutable std::mutex mutex_;
    SlotRing<Packet> ring_;

    bool primed_ = false;
    int target_delay_ms_;
    bool pace_by_sender_ts_;
    int max_excess_ms_;
    size_t max_queue_size_;

    std::optional<Clock::time_point> first_push_time_;
    std::optional<utils::WallTimestamp> anchor_sender_ts_;
    Clock::time_point playback_origin_{};

    uint64_t played_count_ = 0;
    uint64_t drop_count_   = 0;
    uint64_t miss_count_   = 0;
};
