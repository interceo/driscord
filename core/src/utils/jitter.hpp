#pragma once

#include "slot_ring.hpp"
#include "spinlock.hpp"
#include "time.hpp"

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
        : target_delay_(
              std::max(std::chrono::milliseconds(1),
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      target_delay)))
    {
    }

    // Returns true if the packet was dropped (ring full).
    bool push(const uint64_t seq, Ptr&& data)
    {
        std::scoped_lock lk(mutex_);

        const auto now = utils::Now();
        if (!first_push_time_) [[unlikely]] {
            first_push_time_ = now;
        }

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
            if (utils::Elapsed(*first_push_time_, now) < target_delay_) {
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

    utils::Duration target_delay() const { return target_delay_; }

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
    }

private:
    mutable utils::SpinLock mutex_;
    mutable SlotRing<Packet> ring_;

    bool primed_ = false;
    utils::Duration target_delay_;

    std::optional<utils::Timestamp> first_push_time_;
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
