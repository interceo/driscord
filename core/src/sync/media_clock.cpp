#include "media_clock.hpp"

#include "config.hpp"

#include <algorithm>
#include <mutex>

namespace avsync {

bool MediaClock::observe(const Stream stream,
    const int64_t sender_ts_us,
    const int64_t local_now_us) noexcept
{
    StreamState& s = streams_[static_cast<size_t>(stream)];

    // The sender's clock is monotonic within one process, so time only moves
    // backwards if that process restarted. Everything we have measured belongs
    // to the old timeline.
    const bool restarted = s.seen
        && sender_ts_us + sync_defaults::kSenderRestartUs < s.last_sender_ts_us;
    if (restarted) [[unlikely]] {
        reset();
    }

    s.seen = true;
    s.last_sender_ts_us = sender_ts_us;

    s.estimator.observe(local_now_us - sender_ts_us);
    if (s.estimator.ready()) {
        s.min_owd_us.store(s.estimator.min_owd_us(), std::memory_order_relaxed);
        s.variation_us.store(s.estimator.p95_variation_us(), std::memory_order_relaxed);
        s.ready.store(true, std::memory_order_release);
    }

    recompute(local_now_us);
    return restarted;
}

void MediaClock::recompute(const int64_t local_now_us) noexcept
{
    std::scoped_lock lk(recompute_lock_);

    // The clock offset is a property of the pair of machines, not of a stream,
    // so the smallest one-way delay seen on any stream is the best estimate.
    // A stream with a structurally larger delay — video, which cannot leave the
    // sender until every chunk of the frame is out — then shows up as a
    // constant excess above this shared baseline, which is exactly the extra
    // buffering it needs.
    int64_t offset = INT64_MAX;
    for (const StreamState& s : streams_) {
        if (s.ready.load(std::memory_order_acquire)) {
            offset = std::min(offset, s.min_owd_us.load(std::memory_order_relaxed));
        }
    }
    if (offset == INT64_MAX) {
        return; // nothing measurable yet
    }
    offset_us_.store(offset, std::memory_order_relaxed);

    const auto need = [&](const Stream stream) -> int64_t {
        const StreamState& s = streams_[static_cast<size_t>(stream)];
        if (!s.ready.load(std::memory_order_acquire)) {
            return -1;
        }
        const int64_t excess = s.min_owd_us.load(std::memory_order_relaxed) - offset;
        return excess + s.variation_us.load(std::memory_order_relaxed);
    };

    const int64_t audio_need = need(Stream::Audio);
    const int64_t video_need = video_active_.load(std::memory_order_relaxed)
        ? need(Stream::Video)
        : -1;

    int64_t desired = std::max(audio_need, video_need);
    if (desired < 0) {
        return;
    }
    desired = std::clamp(desired + sync_defaults::kDelayMarginUs,
        sync_defaults::kMinDelayUs, sync_defaults::kMaxDelayUs);

    const int64_t current = target_delay_us_.load(std::memory_order_relaxed);
    if (desired > current) {
        // Grow at once: being short is an underrun, which is audible now.
        target_delay_us_.store(desired, std::memory_order_relaxed);
        last_decay_us_ = local_now_us;
    } else if (desired < current) {
        // Shrink in steps. Cutting the target immediately would mean throwing
        // away audio that is already buffered and due to be played.
        if (local_now_us - last_decay_us_ >= sync_defaults::kDelayDecayIntervalUs) {
            const int64_t next = std::max(desired, current - sync_defaults::kDelayDecayStepUs);
            target_delay_us_.store(next, std::memory_order_relaxed);
            last_decay_us_ = local_now_us;
        }
    }

    ready_.store(true, std::memory_order_release);
}

void MediaClock::set_video_active(const bool active) noexcept
{
    video_active_.store(active, std::memory_order_relaxed);
}

void MediaClock::reset() noexcept
{
    for (StreamState& s : streams_) {
        s.estimator.reset();
        s.last_sender_ts_us = 0;
        s.seen = false;
        s.min_owd_us.store(0, std::memory_order_relaxed);
        s.variation_us.store(-1, std::memory_order_relaxed);
        s.ready.store(false, std::memory_order_release);
    }
    {
        std::scoped_lock lk(recompute_lock_);
        last_decay_us_ = 0;
    }
    offset_us_.store(0, std::memory_order_relaxed);
    target_delay_us_.store(0, std::memory_order_relaxed);
    ready_.store(false, std::memory_order_release);
}

} // namespace avsync
