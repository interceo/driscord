#pragma once

#include "clock_skew.hpp"
#include "slot_ring.hpp"
#include "time.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace utils {

template <typename T>
class Jitter {
public:
    using Ptr = std::unique_ptr<T>;

    struct Packet {
        Ptr data { };
    };

    struct PopResult {
        Ptr data;
        bool missed = false;
    };

    explicit Jitter() = default;

    PushStatus push(const uint64_t seq, const utils::WallTimestamp sender_ts, T&& data)
    {
        std::scoped_lock lk(mutex_);

        const auto now = utils::Now();
        if (!first_push_time_) [[unlikely]] {
            first_push_time_ = now;
        }

        skew_.update(sender_ts);

        return ring_.push(seq, Packet { std::make_unique<T>(std::move(data)), now });
    }

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
        return {
            std::move(result.data.data),
            false,
        };
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
            if (pred(*peek->data->data)) {
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

    int64_t ow_delay_ms() const { return skew_.median_ms(); }

    void reset()
    {
        std::scoped_lock lk(mutex_);
        ring_.reset();
        primed_ = false;
        first_push_time_.reset();
        skew_.reset();
    }

private:
    mutable std::mutex mutex_;
    SlotRing<Packet> ring_;

    bool primed_ = false;

    std::optional<utils::Timestamp> first_push_time_;

    ClockSkewEstimator skew_;
    std::optional<utils::Timestamp> last_adapt_time_;
};

} // namespace utils
