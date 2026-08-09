#pragma once

#include "sync/playout_policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Alternative playout-delay policies, for comparison only. Nothing here ships;
// they exist so "our adaptive buffer is better than X" is a measurement.

namespace test_util {

// The naive baseline: a constant buffer, blind to the network.
class FixedDelayPolicy final : public avsync::PlayoutPolicy {
public:
    explicit FixedDelayPolicy(int64_t delay_ms)
        : delay_us_(delay_ms * 1000)
    {
    }

    int64_t target_delay_us(const avsync::StreamObservation& audio,
        const avsync::StreamObservation& video,
        bool,
        int64_t,
        int64_t,
        int64_t) override
    {
        if (!audio.ready && !video.ready) {
            return -1;
        }
        return delay_us_;
    }

    void reset() noexcept override { }

private:
    int64_t delay_us_;
};

// Ramjee et al. 1994, algorithm 4, with the spike detector from Moon et al.
// 1998: an exponentially weighted mean and deviation of the one-way delay,
// except during a delay spike, when the estimate follows the spike directly
// rather than averaging through it.
class EwmaSpikePolicy final : public avsync::PlayoutPolicy {
public:
    explicit EwmaSpikePolicy(avsync::DelayParams params = { })
        : params_(params)
    {
    }

    void observe(Stream stream, int64_t sender_ts_us, int64_t local_now_us) override
    {
        State& s = stream == Stream::Audio ? audio_ : video_;
        const int64_t owd = local_now_us - sender_ts_us;
        if (!s.seen) {
            s.seen = true;
            s.mean = static_cast<double>(owd);
            s.var = 0.0;
            s.min_owd = owd;
            s.last_owd = owd;
            return;
        }

        s.min_owd = std::min(s.min_owd, owd);
        const double delta = static_cast<double>(owd - s.last_owd);

        if (!s.in_spike) {
            // A jump far beyond the current variation is a route change or a
            // burst, not jitter: averaging through it under-buffers for as long
            // as it lasts.
            if (std::fabs(delta) > 2.0 * s.var + 800.0) {
                s.in_spike = true;
                s.spike_var = 0.0;
            }
        } else {
            s.spike_var = s.spike_var / 2.0 + std::fabs(2.0 * static_cast<double>(owd)
                                                   - s.last_owd - s.mean)
                    / 8.0;
            if (s.spike_var <= 63.0) {
                s.in_spike = false;
            }
        }

        const double a = s.in_spike ? 0.5 : kAlpha;
        s.mean = a * s.mean + (1.0 - a) * static_cast<double>(owd);
        s.var = a * s.var + (1.0 - a) * std::fabs(static_cast<double>(owd) - s.mean);
        s.last_owd = owd;
    }

    int64_t target_delay_us(const avsync::StreamObservation& audio,
        const avsync::StreamObservation& video,
        const bool video_active,
        const int64_t offset_us,
        int64_t,
        int64_t) override
    {
        const auto need = [&](const State& s, const avsync::StreamObservation& o) -> int64_t {
            if (!o.ready || !s.seen) {
                return -1;
            }
            // p = mean + 4*var, expressed above the shared clock offset.
            const auto p = static_cast<int64_t>(s.mean + 4.0 * s.var);
            return std::max<int64_t>(0, p - offset_us);
        };

        const int64_t desired = std::max(need(audio_, audio),
            video_active ? need(video_, video) : -1);
        if (desired < 0) {
            return -1;
        }
        const int64_t floor_us = video_active
            ? std::max(params_.min_delay_us, params_.video_floor_us)
            : params_.min_delay_us;
        return std::clamp(desired, floor_us, params_.max_delay_us);
    }

    void reset() noexcept override
    {
        audio_ = State { };
        video_ = State { };
    }

private:
    static constexpr double kAlpha = 0.998002; // Ramjee's weighting

    struct State {
        bool seen = false;
        bool in_spike = false;
        double mean = 0.0;
        double var = 0.0;
        double spike_var = 0.0;
        int64_t min_owd = 0;
        int64_t last_owd = 0;
    };

    avsync::DelayParams params_;
    State audio_;
    State video_;
};

} // namespace test_util
