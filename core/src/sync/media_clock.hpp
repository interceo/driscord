#pragma once

#include "delay_estimator.hpp"
#include "playout_policy.hpp"
#include "utils/spinlock.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace avsync {

class MediaClock {
public:
    enum class Stream : uint8_t { Audio = 0,
        Video = 1,
        kCount = 2 };

    explicit MediaClock(std::unique_ptr<PlayoutPolicy> policy = make_default_policy())
        : policy_(std::move(policy))
    {
    }

    bool observe(Stream stream, int64_t sender_ts_us, int64_t local_now_us) noexcept;

    int64_t deadline_us(const int64_t sender_ts_us) const noexcept
    {
        return sender_ts_us + offset_us_.load(std::memory_order_relaxed)
            + target_delay_us_.load(std::memory_order_relaxed);
    }

    int64_t target_delay_us() const noexcept
    {
        return target_delay_us_.load(std::memory_order_relaxed);
    }

    int64_t offset_us() const noexcept
    {
        return offset_us_.load(std::memory_order_relaxed);
    }

    int64_t stream_delay_percentile_us(Stream stream, int percent) const noexcept;
    uint64_t stream_sample_count(Stream stream) const noexcept;
    bool stream_ready(Stream stream) const noexcept;
    void set_stream_playout_ts(Stream stream, int64_t sender_ts_us) noexcept;
    int64_t stream_playout_ts(Stream stream) const noexcept;

    bool ready() const noexcept
    {
        return ready_.load(std::memory_order_relaxed);
    }

    void set_video_active(bool active) noexcept;
    bool video_active() const noexcept
    {
        return video_active_.load(std::memory_order_relaxed);
    }

    void reset() noexcept;

private:
    static constexpr size_t kStreams = static_cast<size_t>(Stream::kCount);

    struct StreamState {
        DelayEstimator estimator; // producer thread only
        int64_t last_sender_ts_us = 0; // producer thread only
        bool seen = false; // producer thread only

        // Published for the recompute step, which any producer may run.
        std::atomic<int64_t> min_owd_us { 0 };
        std::atomic<int64_t> variation_us { -1 };
        std::atomic<int64_t> p50_variation_us { -1 };
        std::atomic<int64_t> p95_variation_us { -1 };
        std::atomic<int64_t> p99_variation_us { -1 };
        std::atomic<uint64_t> sample_count { 0 };
        std::atomic<int64_t> playout_ts_us { 0 };
        std::atomic<bool> ready { false };
    };

    void recompute(int64_t local_now_us) noexcept;

    StreamState streams_[kStreams];

    utils::SpinLock recompute_lock_;
    std::unique_ptr<PlayoutPolicy> policy_;

    std::atomic<int64_t> offset_us_ { 0 };
    std::atomic<int64_t> target_delay_us_ { 0 };
    std::atomic<bool> ready_ { false };
    std::atomic<bool> video_active_ { false };
};

} // namespace avsync
