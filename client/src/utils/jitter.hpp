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

    explicit JitterBuffer(int target_delay_ms, bool rate_limit_pop = false)
        : target_delay_ms_(std::max(1, target_delay_ms)), rate_limit_(rate_limit_pop) {}

    void set_packet_duration(Duration d) {
        std::scoped_lock lk(mutex_);
        packet_duration_ = d;
    }

    void push(uint64_t seq, T data, utils::WallTimestamp sender_ts = {}) {
        std::scoped_lock lk(mutex_);

        if (!first_push_time_) {
            first_push_time_ = Clock::now();
        }

        if (primed_ && seq < next_seq_) {
            ++drop_count_;
            return;
        }

        while (map_.size() >= kMaxQueue) {
            map_.erase(map_.begin());
            ++drop_count_;
        }

        map_.emplace(seq, Packet{std::move(data), sender_ts});
    }

    // Returns nullopt while buffering or rate-limited.
    // Returns a default Packet on a sequence miss.
    std::optional<Packet> pop() {
        std::scoped_lock lk(mutex_);
        const auto now = Clock::now();

        // Wait target_delay_ms from the first push before returning anything.
        if (!first_push_time_ || now - *first_push_time_ < std::chrono::milliseconds(target_delay_ms_)) {
            return std::nullopt;
        }

        if (map_.empty()) {
            return std::nullopt;
        }

        if (!primed_) {
            primed_ = true;
            next_seq_ = map_.begin()->first;
            playback_origin_ = now;
            played_count_ = 0;
            LOG_INFO()
                << "[jitter] PRIMED q=" << map_.size() << " seq=" << next_seq_ << " delay=" << target_delay_ms_ << "ms";
        }

        // Rate-limit: at most one packet per packet_duration.
        if (rate_limit_ && packet_duration_.count() > 0) {
            if (now < playback_origin_ + played_count_ * packet_duration_) {
                return std::nullopt;
            }
        }

        auto it = map_.find(next_seq_);
        ++next_seq_;
        ++played_count_;

        if (it != map_.end()) {
            auto pkt = std::move(it->second);
            map_.erase(it);
            return pkt;
        }

        ++miss_count_;
        return Packet{};
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
        if (packet_duration_.count() <= 0) {
            return 0;
        }
        return map_.size() * static_cast<size_t>(packet_duration_.count()) / 1000;
    }

    void reset() {
        std::scoped_lock lk(mutex_);
        map_.clear();
        primed_ = false;
        next_seq_ = 0;
        first_push_time_.reset();
        played_count_ = 0;
        drop_count_ = 0;
        miss_count_ = 0;
    }

    Stats stats() const {
        std::scoped_lock lk(mutex_);
        size_t buf_ms = 0;
        if (packet_duration_.count() > 0) {
            buf_ms = map_.size() * static_cast<size_t>(packet_duration_.count()) / 1000;
        }
        return {primed_, map_.size(), buf_ms, drop_count_, miss_count_};
    }

private:
    static constexpr size_t kMaxQueue = 64;

    mutable std::mutex mutex_;
    std::map<uint64_t, Packet> map_;

    int target_delay_ms_;
    bool rate_limit_;
    Duration packet_duration_{0};

    bool primed_ = false;
    uint64_t next_seq_ = 0;

    std::optional<Clock::time_point> first_push_time_;
    Clock::time_point playback_origin_{};
    uint64_t played_count_ = 0;

    uint64_t drop_count_ = 0;
    uint64_t miss_count_ = 0;
};
