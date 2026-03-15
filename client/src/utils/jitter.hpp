#pragma once

#include "log.hpp"
#include "time.hpp"

#include <atomic>
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
// Rate-limited mode (rate_limit_pop = true): pop() paces output so that at most
// one packet is returned per packet_duration interval.  Use for video where the
// consumer runs faster than the frame rate.
//
// Thread safety: push() and pop() may be called from different threads.
// T must be default-constructible and move-constructible.

template <typename T>
class JitterBuffer {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::microseconds;

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

    explicit JitterBuffer(int target_delay_ms, bool rate_limit_pop = false)
        : target_delay_ms_(std::max(1, target_delay_ms)), rate_limit_pop_(rate_limit_pop) {}

    void set_packet_duration(Duration d) { packet_duration_.store(d); }

    void push(uint64_t seq, T data, utils::WallTimestamp sender_ts = {}) {
        auto expected = Clock::time_point::max();
        first_push_time_.compare_exchange_strong(expected, Clock::now());

        std::scoped_lock lk(mutex_);
        if (map_.size() >= kMaxQueue) {
            const auto diff = map_.size() - kMaxQueue;
            map_.erase(map_.begin(), std::next(map_.begin(), diff));
            drop_count_ += diff;
        }

        map_.emplace(seq, Packet{std::move(data), sender_ts});
    }

    // Returns nullopt while buffering or rate-limited.
    std::optional<Packet> pop() {
        const auto now = Clock::now();

        const auto first = first_push_time_.load();
        if (first == Clock::time_point::max()) {
            return std::nullopt;
        }

        if (!primed_.load()) {
            if (now - first < std::chrono::milliseconds(target_delay_ms_)) {
                return std::nullopt;
            }
            primed_.store(true);
        }

        if (rate_limit_pop_) {
            const auto dur = packet_duration_.load();
            if (dur.count() > 0) {
                const auto last = last_pop_time_.load();
                if (last != Clock::time_point{} && (now - last) < dur) {
                    return std::nullopt;
                }
            }
        }

        std::scoped_lock lk(mutex_);
        if (auto it = map_.begin(); it != map_.end()) {
            auto pkt = std::move(it->second);
            map_.erase(it);
            if (rate_limit_pop_) {
                last_pop_time_.store(now);
            }
            return pkt;
        }

        ++miss_count_;
        return std::nullopt;
    }

    bool primed() const { return primed_.load(); }

    size_t queue_size() const {
        std::scoped_lock lk(mutex_);
        return map_.size();
    }

    size_t buffered_ms() const {
        std::scoped_lock lk(mutex_);

        const auto d = packet_duration_.load();
        if (d.count() <= 0) {
            return 0;
        }
        return map_.size() * static_cast<size_t>(d.count()) / 1000u;
    }

    void reset() {
        std::scoped_lock lk(mutex_);

        map_.clear();
        primed_.store(false);
        first_push_time_.store(Clock::time_point::max());
        packet_duration_.store(Duration::zero());
        last_pop_time_.store(Clock::time_point{});
        drop_count_ = 0;
        miss_count_ = 0;
    }

    Stats stats() const {
        std::scoped_lock lk(mutex_);

        size_t buf_ms = 0;
        const auto d = packet_duration_.load();
        if (d.count() > 0) {
            buf_ms = map_.size() * static_cast<size_t>(d.count()) / 1000;
        }
        return {
            primed_.load(),
            map_.size(),
            buf_ms,
            drop_count_,
            miss_count_,
        };
    }

private:
    static constexpr size_t kMaxQueue = 640;

    mutable std::mutex mutex_;
    std::map<uint64_t, Packet> map_;

    std::atomic<bool> primed_ = false;

    int target_delay_ms_;
    bool rate_limit_pop_;
    std::atomic<Duration> packet_duration_{Duration::zero()};

    std::atomic<Clock::time_point> first_push_time_{Clock::time_point::max()};
    std::atomic<Clock::time_point> last_pop_time_{Clock::time_point{}};

    uint64_t drop_count_ = 0;
    uint64_t miss_count_ = 0;
};
