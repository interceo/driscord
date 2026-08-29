#pragma once

#include "webrtc/google_webrtc_voice_session.hpp"

#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace driscord::media::detail {

class DecodedAudioSink final : public webrtc::AudioTrackSinkInterface {
public:
    using Callback = std::function<void(std::string_view mid,
        const void* audio_data,
        int bits_per_sample,
        int sample_rate,
        size_t channels,
        size_t frames)>;

    DecodedAudioSink(std::string mid, Callback callback);

    [[nodiscard]] const std::string& mid() const noexcept;

    void OnData(const void* audio_data,
        int bits_per_sample,
        int sample_rate,
        size_t number_of_channels,
        size_t number_of_frames) override;

private:
    const std::string mid_;
    const Callback callback_;
};

class StatsObserver : public webrtc::RTCStatsCollectorCallback {
public:
    using Callback = std::function<void(
        const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)>;

    explicit StatsObserver(Callback callback);

    void OnStatsDelivered(
        const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
        override;

private:
    const Callback callback_;
};

[[nodiscard]] VoiceSessionStats parse_voice_stats(
    const webrtc::RTCStatsReport& report);

}
