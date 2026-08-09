#pragma once

#include "config.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>

namespace avsync {

// What one stream's arrivals look like, already reduced by DelayEstimator.
struct StreamObservation {
    bool ready = false;
    int64_t min_owd_us = 0;
    int64_t variation_us = 0; // p95 above min_owd_us
};

// Tunables of the default policy. Separate from sync_defaults because those are
// constexpr, and a parameter sweep should not need a recompile.
struct DelayParams {
    int64_t min_delay_us = sync_defaults::kMinDelayUs;
    int64_t max_delay_us = sync_defaults::kMaxDelayUs;
    int64_t margin_us = sync_defaults::kDelayMarginUs;
    int64_t decay_step_us = sync_defaults::kDelayDecayStepUs;
    int64_t decay_interval_us = sync_defaults::kDelayDecayIntervalUs;
    int64_t video_floor_us
        = static_cast<int64_t>(stream_defaults::kScreenBufferMs) * 1000;
};

// Decides how much playout delay both streams of one peer share.
//
// Extracted from MediaClock so the same traffic can be replayed through
// alternative implementations and compared. The clock offset is not a policy
// decision — it is a property of the pair of machines — and stays in MediaClock.
class PlayoutPolicy {
public:
    enum class Stream : uint8_t { Audio = 0,
        Video = 1 };

    virtual ~PlayoutPolicy() = default;

    // Every arrival, before it has been reduced to a StreamObservation. The
    // default policy works from the reduced form; alternatives that estimate
    // delay from the packet stream itself need the raw pair.
    virtual void observe(Stream, int64_t /*sender_ts_us*/, int64_t /*local_now_us*/) { }

    // Returns the new target delay, or -1 to leave the current one alone.
    // Called under MediaClock's recompute lock, so implementations may hold
    // mutable state without further synchronisation.
    virtual int64_t target_delay_us(const StreamObservation& audio,
        const StreamObservation& video,
        bool video_active,
        int64_t offset_us,
        int64_t current_target_us,
        int64_t now_us)
        = 0;

    virtual void reset() noexcept = 0;
};

// The shipped behaviour: cover the slower stream's structural lag plus its
// jitter, grow immediately, shrink in steps.
class DefaultPlayoutPolicy final : public PlayoutPolicy {
public:
    explicit DefaultPlayoutPolicy(DelayParams params = { })
        : params_(params)
    {
    }

    int64_t target_delay_us(const StreamObservation& audio,
        const StreamObservation& video,
        const bool video_active,
        const int64_t offset_us,
        const int64_t current_target_us,
        const int64_t now_us) override
    {
        const auto need = [&](const StreamObservation& s) -> int64_t {
            if (!s.ready) {
                return -1;
            }
            return (s.min_owd_us - offset_us) + s.variation_us;
        };

        const int64_t audio_need = need(audio);
        const int64_t video_need = video_active ? need(video) : -1;

        int64_t desired = std::max(audio_need, video_need);
        if (desired < 0) {
            return -1;
        }

        const int64_t min_delay_us = video_active
            ? std::max(params_.min_delay_us, params_.video_floor_us)
            : params_.min_delay_us;
        desired = std::clamp(desired + params_.margin_us,
            min_delay_us, params_.max_delay_us);

        if (desired > current_target_us) {
            // Grow at once: being short is an underrun, which is audible now.
            last_decay_us_ = now_us;
            return desired;
        }
        if (desired < current_target_us) {
            // Shrink in steps. Cutting the target immediately would mean
            // throwing away audio that is already buffered and due to be played.
            if (now_us - last_decay_us_ >= params_.decay_interval_us) {
                last_decay_us_ = now_us;
                return std::max(desired, current_target_us - params_.decay_step_us);
            }
        }
        return current_target_us;
    }

    void reset() noexcept override { last_decay_us_ = 0; }

private:
    DelayParams params_;
    int64_t last_decay_us_ = 0;
};

inline std::unique_ptr<PlayoutPolicy> make_default_policy(DelayParams p = { })
{
    return std::make_unique<DefaultPlayoutPolicy>(p);
}

} // namespace avsync
