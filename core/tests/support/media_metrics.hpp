#pragma once

#include "wait_helpers.hpp"

#include "webrtc/google_webrtc_media_types.hpp"
#include "webrtc/google_webrtc_screen_session.hpp"
#include "webrtc/google_webrtc_voice_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace test_util {

inline double concealment_rate(uint64_t concealed_samples,
    const driscord::media::AudioReceiveStats& audio)
{
    if (audio.total_samples_received == 0) {
        return 0.0;
    }
    return static_cast<double>(concealed_samples)
        / static_cast<double>(audio.total_samples_received);
}

inline double concealment_rate(const driscord::media::VoiceInboundRtpStats& s)
{
    return concealment_rate(s.concealed_samples, s.audio);
}

inline double concealment_rate(const driscord::media::ScreenInboundRtpStats& s)
{
    return concealment_rate(s.concealed_samples, s.audio);
}

inline double concealment_burst_length(uint64_t concealed_samples,
    const driscord::media::AudioReceiveStats& audio)
{
    if (audio.concealment_events == 0) {
        return 0.0;
    }
    return static_cast<double>(concealed_samples)
        / static_cast<double>(audio.concealment_events);
}

inline double harmonic_framerate(const driscord::media::VideoReceiveStats& v)
{
    if (v.total_squared_inter_frame_delay_seconds <= 0.0) {
        return 0.0;
    }
    return v.total_inter_frame_delay_seconds
        / v.total_squared_inter_frame_delay_seconds;
}

inline double freeze_ratio(const driscord::media::VideoReceiveStats& v,
    double session_seconds)
{
    if (session_seconds <= 0.0) {
        return 0.0;
    }
    return v.total_freezes_duration_seconds / session_seconds;
}

inline std::optional<double> playout_skew_ms(
    const driscord::media::RtpReceiveStats& audio,
    const driscord::media::RtpReceiveStats& video)
{
    if (audio.estimated_playout_timestamp_ms < 0.0
        || video.estimated_playout_timestamp_ms < 0.0) {
        return std::nullopt;
    }
    return audio.estimated_playout_timestamp_ms
        - video.estimated_playout_timestamp_ms;
}

inline double repair_ratio(const driscord::media::RtpReceiveStats& rtp,
    int64_t packets_lost)
{
    const auto recovered
        = static_cast<double>(rtp.retransmitted_packets_received);
    const auto lost = static_cast<double>(std::max<int64_t>(packets_lost, 0));
    if (recovered + lost <= 0.0) {
        return 1.0;
    }
    return recovered / (recovered + lost);
}

inline double percentile(std::vector<double> values, double p)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double clamped = std::clamp(p, 0.0, 100.0);
    const auto rank = static_cast<size_t>(
        std::ceil(clamped / 100.0 * static_cast<double>(values.size())));
    return values[rank == 0 ? 0 : rank - 1];
}

inline bool is_non_decreasing(const std::vector<double>& values)
{
    return std::is_sorted(values.begin(), values.end());
}

template <typename Stats, typename Session>
std::vector<Stats> sample_stats(Session& session,
    size_t count,
    std::chrono::milliseconds period = std::chrono::milliseconds { 1000 })
{
    std::vector<Stats> samples;
    samples.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        auto collector = std::make_shared<EventCollector<Stats>>();
        if (!session.get_stats(
                [collector](Stats stats) { collector->push(std::move(stats)); })) {
            break;
        }
        if (!collector->wait_for_count(1)) {
            break;
        }
        samples.push_back(collector->snapshot().front());
        if (i + 1 < count) {
            std::this_thread::sleep_for(period);
        }
    }
    return samples;
}

enum class GateMode {
    Enforce,
    Soft,
};

inline GateMode quality_gate_mode()
{
    const char* value = std::getenv("DRISCORD_QUALITY_ENFORCE");
    if (value != nullptr && std::string_view { value } == "soft") {
        return GateMode::Soft;
    }
    return GateMode::Enforce;
}

namespace detail {

    inline bool report_gate(std::string_view name,
        double value,
        std::string_view cmp,
        double threshold,
        bool pass)
    {
        const GateMode mode = quality_gate_mode();
        std::printf("{\"gate\":\"%.*s\",\"value\":%.6f,\"cmp\":\"%.*s\","
                    "\"threshold\":%.6f,\"pass\":%s,\"mode\":\"%s\"}\n",
            static_cast<int>(name.size()), name.data(), value,
            static_cast<int>(cmp.size()), cmp.data(), threshold,
            pass ? "true" : "false",
            mode == GateMode::Soft ? "soft" : "enforce");
        std::fflush(stdout);
        return pass || mode == GateMode::Soft;
    }

}

inline bool gate_le(std::string_view name, double value, double threshold)
{
    return detail::report_gate(name, value, "<=", threshold, value <= threshold);
}

inline bool gate_ge(std::string_view name, double value, double threshold)
{
    return detail::report_gate(name, value, ">=", threshold, value >= threshold);
}

}
