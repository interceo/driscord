#pragma once

// Internal helper shared by the voice and screen stats parsers: lifts
// RTCStatsReport rows into the plain value groups declared in
// google_webrtc_media_types.hpp. Include only from adapter .cpp translation
// units — WebRTC types must not reach public headers.

#include "webrtc/google_webrtc_media_types.hpp"

#include "api/stats/rtcstats_objects.h"

#include <string>

namespace driscord::media::detail {

inline RtpReceiveStats lift_rtp_receive_stats(
    const webrtc::RTCInboundRtpStreamStats& inbound)
{
    RtpReceiveStats result;
    result.jitter_seconds = inbound.jitter.value_or(0.0);
    result.nack_count = inbound.nack_count.value_or(0);
    result.packets_discarded = inbound.packets_discarded.value_or(0);
    result.retransmitted_packets_received
        = inbound.retransmitted_packets_received.value_or(0);
    result.estimated_playout_timestamp_ms
        = inbound.estimated_playout_timestamp.value_or(-1.0);
    return result;
}

inline AudioReceiveStats lift_audio_receive_stats(
    const webrtc::RTCInboundRtpStreamStats& inbound)
{
    AudioReceiveStats result;
    result.total_samples_received = inbound.total_samples_received.value_or(0);
    result.silent_concealed_samples
        = inbound.silent_concealed_samples.value_or(0);
    result.concealment_events = inbound.concealment_events.value_or(0);
    result.inserted_samples_for_deceleration
        = inbound.inserted_samples_for_deceleration.value_or(0);
    result.removed_samples_for_acceleration
        = inbound.removed_samples_for_acceleration.value_or(0);
    result.audio_level = inbound.audio_level.value_or(0.0);
    result.total_audio_energy = inbound.total_audio_energy.value_or(0.0);
    result.total_samples_duration_seconds
        = inbound.total_samples_duration.value_or(0.0);
    return result;
}

inline VideoReceiveStats lift_video_receive_stats(
    const webrtc::RTCInboundRtpStreamStats& inbound)
{
    VideoReceiveStats result;
    result.frames_received = inbound.frames_received.value_or(0);
    result.frames_per_second = inbound.frames_per_second.value_or(0.0);
    result.frame_width = inbound.frame_width.value_or(0);
    result.frame_height = inbound.frame_height.value_or(0);
    result.freeze_count = inbound.freeze_count.value_or(0);
    result.total_freezes_duration_seconds
        = inbound.total_freezes_duration.value_or(0.0);
    result.pause_count = inbound.pause_count.value_or(0);
    result.total_pauses_duration_seconds
        = inbound.total_pauses_duration.value_or(0.0);
    result.total_inter_frame_delay_seconds
        = inbound.total_inter_frame_delay.value_or(0.0);
    result.total_squared_inter_frame_delay_seconds
        = inbound.total_squared_inter_frame_delay.value_or(0.0);
    result.pli_count = inbound.pli_count.value_or(0);
    result.fir_count = inbound.fir_count.value_or(0);
    result.qp_sum = inbound.qp_sum.value_or(0);
    return result;
}

inline QualityLimitation lift_quality_limitation(
    const webrtc::RTCOutboundRtpStreamStats& outbound)
{
    const std::string reason
        = outbound.quality_limitation_reason.value_or(std::string { "none" });
    if (reason == "none") {
        return QualityLimitation::None;
    }
    if (reason == "cpu") {
        return QualityLimitation::Cpu;
    }
    if (reason == "bandwidth") {
        return QualityLimitation::Bandwidth;
    }
    return QualityLimitation::Other;
}

} // namespace driscord::media::detail
