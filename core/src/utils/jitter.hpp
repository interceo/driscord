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

    struct Stats {
        bool primed = false;
        size_t queue_size = 0;
        uint64_t drop_count = 0;
        uint64_t miss_count = 0;
    };

    explicit JitterBuffer(const utils::Duration target_delay)
        : target_delay_(
              std::max(std::chrono::milliseconds(1),
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      target_delay)))
    {
    }

    void push(const uint64_t seq, Ptr&& data)
    {
        std::scoped_lock lk(mutex_);

        const auto now = utils::Now();
        if (!first_push_time_) [[unlikely]] {
            first_push_time_ = now;
        }

        if (!ring_.push(seq, Packet { std::move(data), now })) {
            ++drop_count_;
        }
    }

    Ptr pop() noexcept
    {
        std::unique_lock lk(mutex_, std::try_to_lock);
        if (!lk.owns_lock()) [[unlikely]] {
            return nullptr;
        }

        const auto now = utils::Now();
        if (!first_push_time_) [[unlikely]] {
            return nullptr;
        }

        if (!primed_) [[unlikely]] {
            if (utils::Elapsed(*first_push_time_, now) < target_delay_) {
                return nullptr;
            }
            primed_ = true;
        }

        if (ring_.empty()) {
            return nullptr;
        }

        auto peek = ring_.peek_next();
        if (!peek) {
            return nullptr;
        }

        if (peek->skipped > 0) {
            ring_.advance_seq();
            ++miss_count_;
            return nullptr;
        }

        auto result = ring_.consume_peeked(0);
        ++played_count_;

        return std::move(result.data.data);
    }

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
            ++drop_count_;
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
            ++drop_count_;
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
        played_count_ = 0;
        drop_count_ = 0;
        miss_count_ = 0;
    }

    Stats stats() const
    {
        std::scoped_lock lk(mutex_);
        return {
            primed_,
            ring_.size(),
            drop_count_,
            miss_count_,
        };
    }

private:
    mutable utils::SpinLock mutex_;
    mutable SlotRing<Packet> ring_;

    bool primed_ = false;
    utils::Duration target_delay_;

    std::optional<utils::Timestamp> first_push_time_;

    uint64_t played_count_ = 0;
    uint64_t drop_count_ = 0;
    uint64_t miss_count_ = 0;
};

template <typename Frame>
class Jitter {
public:
    using JitterBuf = JitterBuffer<Frame>;
    using Stats = JitterBuf::Stats;
    using Ptr = JitterBuf::Ptr;

    explicit Jitter(const utils::Duration target_delay)
        : buf_(target_delay)
    {
    }

    void push(uint64_t seq, Frame&& frame)
    {
        if (frame.empty()) {
            return;
        }
        buf_.push(seq, std::make_unique<Frame>(std::move(frame)));
    }

    Ptr pop() { return buf_.pop(); }

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

    Stats stats() const { return buf_.stats(); }

private:
    JitterBuf buf_;
};

} // namespace utils
