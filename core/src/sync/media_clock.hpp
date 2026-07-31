#pragma once

#include "delay_estimator.hpp"
#include "utils/spinlock.hpp"

#include <atomic>
#include <cstdint>

namespace avsync {

// The single playout timeline for one remote peer.
//
// A peer's audio and video are two independent streams with very different
// packet shapes — a 20 ms Opus frame is one packet, a video frame is hundreds
// of chunks — so they arrive with different delays and different jitter. Both
// are stamped from the same monotonic clock on the sending side, which is what
// makes a shared timeline possible at all.
//
// Every unit of media is played at
//
//     deadline = sender_ts + offset + target_delay
//
// with `offset` and `target_delay` common to both streams. Audio and video
// therefore stay aligned by construction: there is nothing to drift apart, and
// no need to fast-forward one stream by discarding media to catch up with the
// other.
//
// `target_delay` covers whichever stream currently needs more headroom, but
// video only counts while the user is actually watching that peer's screen —
// otherwise a voice-only call would pay video's much larger buffering cost.
//
// Threading: observe() runs on the network threads (one per stream);
// deadline_us(), target_delay_us() and offset_us() are read from the audio
// callback and the render tick and touch nothing but atomics.
class MediaClock {
public:
    enum class Stream : uint8_t { Audio = 0,
        Video = 1,
        kCount = 2 };

    // Records an arrival. Returns true if the peer's timeline restarted and
    // every estimate was dropped, which the caller should treat as a
    // discontinuity (reset decoder state, drop buffered media).
    bool observe(Stream stream, int64_t sender_ts_us, int64_t local_now_us) noexcept;

    // Local monotonic time at which media stamped `sender_ts_us` is due.
    // Compare against utils::MonoClock::now_us().
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

    // Engaged when the user starts watching this peer's screen. Only shifts the
    // target; the audio playout walks to the new value rather than jumping.
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

    // Producers serialise here only to update the smoothed target; readers on
    // the real-time path never touch it.
    utils::SpinLock recompute_lock_;
    int64_t last_decay_us_ = 0;

    std::atomic<int64_t> offset_us_ { 0 };
    std::atomic<int64_t> target_delay_us_ { 0 };
    std::atomic<bool> ready_ { false };
    std::atomic<bool> video_active_ { false };
};

} // namespace avsync
