#pragma once

#include "slot_ring.hpp"
#include "spinlock.hpp"
#include "time.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace utils {

template <typename T>
class JitterBuffer {
public:
    using Ptr = std::unique_ptr<T>;

    struct Packet {
        Ptr data { };
        utils::Timestamp arrival { };
    };

    struct PopResult {
        Ptr data;
        bool missed = false;
    };

    explicit JitterBuffer(const utils::Duration target_delay)
        : initial_delay_(
              std::max(std::chrono::milliseconds(1),
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      target_delay)))
        , adaptive_delay_(initial_delay_)
    {
        intervals_.fill(0);
    }

    // Returns true if the packet was dropped (ring full).
    bool push(const uint64_t seq, Ptr&& data)
    {
        std::scoped_lock lk(mutex_);

        const auto now = utils::Now();
        if (!first_push_time_) [[unlikely]] {
            first_push_time_ = now;
        }

        // Track inter-packet arrival intervals for adaptive delay.
        if (last_arrival_) {
            const auto interval_ms = std::chrono::duration_cast<
                std::chrono::milliseconds>(now - *last_arrival_).count();
            intervals_[interval_write_ % kIntervalWindow] = interval_ms;
            ++interval_write_;
            interval_count_ = std::min(interval_count_ + 1, kIntervalWindow);
            update_adaptive_delay(now);
        }
        last_arrival_ = now;

        return !ring_.push(seq, Packet { std::move(data), now });
    }

    // Returns {data, missed=false} on success, {nullptr, true} on a seq gap
    // (miss), {nullptr, false} when not ready or queue empty.
    PopResult pop() noexcept
    {
        std::unique_lock lk(mutex_, std::try_to_lock);
        if (!lk.owns_lock()) [[unlikely]] {
            return { };
        }

        const auto now = utils::Now();
        if (!first_push_time_) [[unlikely]] {
            return { };
        }

        if (!primed_) [[unlikely]] {
            if (utils::Elapsed(*first_push_time_, now) < adaptive_delay_) {
                return { };
            }
            primed_ = true;
        }

        if (ring_.empty()) {
            return { };
        }

        auto peek = ring_.peek_next();
        if (!peek) {
            return { };
        }

        if (peek->skipped > 0) {
            ring_.advance_seq();
            return { nullptr, true };
        }

        auto result = ring_.consume_peeked(0);
        return { std::move(result.data.data), false };
    }

    // Returns number of evicted packets (each counts as a drop).
    size_t evict_old(const utils::Duration max_delay)
    {
        std::scoped_lock lk(mutex_);
        size_t dropped = 0;
        while (true) {
            auto peek = ring_.peek_next();
            if (!peek) {
                break;
            }
            if (utils::Elapsed(peek->data->arrival) < max_delay) {
                break;
            }
            ring_.consume_peeked(peek->skipped);
            ++dropped;
        }
        return dropped;
    }

    template <typename Pred>
    size_t evict_if(Pred&& pred)
    {
        std::scoped_lock lk(mutex_);
        size_t dropped = 0;
        while (true) {
            auto peek = ring_.peek_next();
            if (!peek) {
                break;
            }
            if (!pred(*peek->data->data)) {
                break;
            }
            ring_.consume_peeked(peek->skipped);
            ++dropped;
        }
        return dropped;
    }

    template <typename F>
    auto with_front(F&& fn) const
        -> std::optional<std::invoke_result_t<F, const T&>>
    {
        std::scoped_lock lk(mutex_);
        auto peek = ring_.peek_next();
        if (!peek) {
            return std::nullopt;
        }
        return fn(*peek->data->data);
    }

    utils::Duration target_delay() const { return adaptive_delay_; }

    int64_t front_age_ms() const
    {
        std::scoped_lock lk(mutex_);
        auto peek = ring_.peek_next();
        if (!peek) {
            return -1;
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            utils::Elapsed(peek->data->arrival))
            .count();
    }

    bool primed() const
    {
        std::scoped_lock lk(mutex_);
        return primed_;
    }

    size_t queue_size() const
    {
        std::scoped_lock lk(mutex_);
        return ring_.size();
    }

    void reset()
    {
        std::scoped_lock lk(mutex_);
        ring_.reset();
        primed_ = false;
        first_push_time_.reset();
        adaptive_delay_ = initial_delay_;
        intervals_.fill(0);
        interval_write_ = 0;
        interval_count_ = 0;
        last_arrival_.reset();
        last_adapt_time_.reset();
    }

private:
    static constexpr size_t kIntervalWindow = 50;
    static constexpr int64_t kMinDelayMs = 10;
    static constexpr int64_t kMaxDelayMs = 400;
    // Decrease rate: at most 1 ms per adaptive update.
    static constexpr int64_t kDecreaseStepMs = 1;

    void update_adaptive_delay(utils::Timestamp now)
    {
        // Need at least 8 samples before adapting.
        if (interval_count_ < 8) {
            return;
        }
        // Rate-limit: adapt at most once every 100ms.
        if (last_adapt_time_
            && utils::Elapsed(*last_adapt_time_, now) < std::chrono::milliseconds(100)) {
            return;
        }
        last_adapt_time_ = now;

        // Sort a copy of the interval window and pick p95.
        std::array<int64_t, kIntervalWindow> sorted {};
        const size_t n = interval_count_;
        for (size_t i = 0; i < n; ++i) {
            // Read from ring starting at (interval_write_ - n).
            sorted[i] = intervals_[(interval_write_ - n + i) % kIntervalWindow];
        }
        std::sort(sorted.begin(), sorted.begin() + n);
        const int64_t p95 = sorted[n * 95 / 100];

        // Target = p95 + small margin, clamped to bounds.
        const int64_t target = std::clamp(p95 + 5, kMinDelayMs, kMaxDelayMs);
        const int64_t current = std::chrono::duration_cast<
            std::chrono::milliseconds>(adaptive_delay_).count();

        if (target > current) {
            // Increase immediately.
            adaptive_delay_ = std::chrono::milliseconds(target);
        } else if (target < current) {
            // Decrease slowly.
            adaptive_delay_ = std::chrono::milliseconds(
                std::max(target, current - kDecreaseStepMs));
        }
    }

    mutable utils::SpinLock mutex_;
    mutable SlotRing<Packet> ring_;

    bool primed_ = false;
    utils::Duration initial_delay_;
    utils::Duration adaptive_delay_;

    std::optional<utils::Timestamp> first_push_time_;

    // Adaptive jitter tracking.
    std::array<int64_t, kIntervalWindow> intervals_ {};
    size_t interval_write_ = 0;
    size_t interval_count_ = 0;
    std::optional<utils::Timestamp> last_arrival_;
    std::optional<utils::Timestamp> last_adapt_time_;
};

template <typename Frame>
class Jitter {
public:
    using JitterBuf = JitterBuffer<Frame>;
    using Ptr = JitterBuf::Ptr;
    using PopResult = JitterBuf::PopResult;

    explicit Jitter(const utils::Duration target_delay)
        : buf_(target_delay)
    {
    }

    // Returns true if the frame was dropped (ring full).
    bool push(uint64_t seq, Frame&& frame)
    {
        if (frame.empty()) {
            return false;
        }
        return buf_.push(seq, std::make_unique<Frame>(std::move(frame)));
    }

    PopResult pop() { return buf_.pop(); }

    size_t evict_old(const utils::Duration max_delay)
    {
        return buf_.evict_old(max_delay);
    }

    size_t evict_before_sender_ts(const utils::WallTimestamp cutoff)
    {
        return buf_.evict_if(
            [cutoff](const Frame& f) { return f.sender_ts < cutoff; });
    }

    std::optional<utils::WallTimestamp> front_effective_ts() const
    {
        const auto td = std::chrono::duration_cast<std::chrono::milliseconds>(
            buf_.target_delay());
        return buf_.with_front([td](const Frame& f) -> utils::WallTimestamp {
            return f.sender_ts + td;
        });
    }

    template <typename F>
    auto with_front(F&& fn) const
        -> std::optional<std::invoke_result_t<F, const Frame&>>
    {
        return buf_.with_front(std::forward<F>(fn));
    }

    bool primed() const { return buf_.primed(); }
    utils::Duration target_delay() const { return buf_.target_delay(); }

    size_t queue_size() const { return buf_.queue_size(); }
    int64_t front_age_ms() const { return buf_.front_age_ms(); }
    void reset() { buf_.reset(); }

private:
    JitterBuf buf_;
};

} // namespace utils
