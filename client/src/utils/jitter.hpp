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

    explicit JitterBuffer(int target_delay_ms) : target_delay_ms_(std::max(1, target_delay_ms)) {}

    void set_packet_duration(Duration d) { packet_duration_.store(d); }

    void push(uint64_t seq, T data, utils::WallTimestamp sender_ts = {}) {
        if (!primed_.load()) {
            first_push_time_.store(Clock::now());
        }

        std::scoped_lock lk(mutex_);
        if (map_.size() >= kMaxQueue) {
            const auto diff = map_.size() - kMaxQueue;
            map_.erase(map_.begin(), std::next(map_.begin(), diff));
            drop_count_ += diff;
        }

        map_.emplace(seq, Packet{std::move(data), sender_ts});
    }

    // Returns nullopt while buffering or rate-limited.
    // Returns a default Packet on a sequence miss.
    std::optional<Packet> pop() {
        const auto now = Clock::now();

        if (!primed_.load()) {
            const auto first_push_time = first_push_time_.load();
            if (const auto diff = now - first_push_time; diff < std::chrono::milliseconds(target_delay_ms_)) {
                return std::nullopt;
            }
            primed_.store(true);
        }

        std::scoped_lock lk(mutex_);
        if (auto it = map_.begin(); it != map_.end()) {
            auto pkt = std::move(it->second);
            map_.erase(it);
            return pkt;
        }

        ++miss_count_;
        return Packet{};
    }

    bool primed() const { return primed_.load(); }

    size_t queue_size() const {
        std::scoped_lock lk(mutex_);
        return map_.size();
    }

    size_t buffered_ms() const {
        std::scoped_lock lk(mutex_);

        const auto packet_duration = packet_duration_.load();
        if (packet_duration.count() <= 0) {
            return 0;
        }
        return map_.size() * static_cast<size_t>(packet_duration.count()) / 1000u;
    }

    void reset() {
        std::scoped_lock lk(mutex_);

        map_.clear();
        primed_.store(false);
        first_push_time_.store(Clock::time_point::max());
        packet_duration_.store(Duration::zero());
        played_count_ = 0;
        drop_count_ = 0;
        miss_count_ = 0;
    }

    Stats stats() const {
        std::scoped_lock lk(mutex_);

        size_t buf_ms = 0;
        const auto packet_duration = packet_duration_.load();
        if (packet_duration.count() > 0) {
            buf_ms = map_.size() * static_cast<size_t>(packet_duration.count()) / 1000;
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
    std::atomic<Duration> packet_duration_{Duration::zero()};

    std::atomic<Clock::time_point> first_push_time_{Clock::time_point::max()};

    uint64_t played_count_ = 0;
    uint64_t drop_count_ = 0;
    uint64_t miss_count_ = 0;
};
