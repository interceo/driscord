#include "webrtc/google_webrtc_voice_adapters.hpp"

#include "api/stats/rtcstats_objects.h"

#include <algorithm>
#include <utility>

namespace driscord::media::detail {

DecodedAudioSink::DecodedAudioSink(std::string mid, Callback callback)
    : mid_(std::move(mid))
    , callback_(std::move(callback))
{
}

const std::string& DecodedAudioSink::mid() const noexcept
{
    return mid_;
}

void DecodedAudioSink::OnData(const void* audio_data,
    int bits_per_sample,
    int sample_rate,
    size_t number_of_channels,
    size_t number_of_frames)
{
    if (callback_) {
        callback_(mid_, audio_data, bits_per_sample, sample_rate,
            number_of_channels, number_of_frames);
    }
}

StatsObserver::StatsObserver(Callback callback)
    : callback_(std::move(callback))
{
}

void StatsObserver::OnStatsDelivered(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
{
    if (callback_) {
        callback_(report);
    }
}

VoiceSessionStats parse_voice_stats(const webrtc::RTCStatsReport& report)
{
    VoiceSessionStats result;
    for (const auto* outbound :
        report.GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
        if (outbound->kind && *outbound->kind != "audio") {
            continue;
        }
        result.packets_sent += outbound->packets_sent.value_or(0);
        result.bytes_sent += outbound->bytes_sent.value_or(0);
    }
    for (const auto* inbound :
        report.GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
        if (inbound->kind && *inbound->kind != "audio") {
            continue;
        }
        result.inbound.push_back(VoiceInboundRtpStats {
            .mid = inbound->mid.value_or(std::string { }),
            .packets_received = inbound->packets_received.value_or(0),
            .bytes_received = inbound->bytes_received.value_or(0),
            .packets_lost = inbound->packets_lost.value_or(0),
            .concealed_samples = inbound->concealed_samples.value_or(0),
            .jitter_buffer_emitted_count = inbound->jitter_buffer_emitted_count.value_or(0),
            .jitter_buffer_delay_seconds = inbound->jitter_buffer_delay.value_or(0.0),
        });
    }
    std::sort(result.inbound.begin(), result.inbound.end(),
        [](const VoiceInboundRtpStats& lhs, const VoiceInboundRtpStats& rhs) {
            return lhs.mid < rhs.mid;
        });
    return result;
}

} // namespace driscord::media::detail
