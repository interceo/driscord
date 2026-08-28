#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace driscord::media {

enum class MediaConnectionState {
    New,
    Connecting,
    Connected,
    Disconnected,
    Failed,
    Closed,
};

using VoiceConnectionState = MediaConnectionState;
using ScreenConnectionState = MediaConnectionState;

struct DecodedAudioFrameView {
    // Valid only for the duration of its callback. Samples are interleaved.
    std::span<const int16_t> samples;
    int sample_rate_hz = 0;
    size_t channels = 0;
    size_t frames = 0;
};

// Outbound video encoder constraint, lifted from qualityLimitationReason.
enum class QualityLimitation {
    None,
    Cpu,
    Bandwidth,
    Other,
};

// Inbound counters shared by every received track kind, lifted from the
// standard RTCStatsReport. Cumulative unless noted otherwise.
struct RtpReceiveStats {
    double jitter_seconds = 0.0;
    uint32_t nack_count = 0;
    uint64_t packets_discarded = 0;
    uint64_t retransmitted_packets_received = 0;
    // Milliseconds on the NTP-anchored playout clock; negative until the
    // first frame/sample plays out. Only the delta against another track's
    // value is meaningful (cross-media sync), never wall-clock arithmetic.
    double estimated_playout_timestamp_ms = -1.0;
};

// NetEq playout counters for one received audio track (a voice microphone or
// screen system audio).
struct AudioReceiveStats {
    uint64_t total_samples_received = 0;
    uint64_t silent_concealed_samples = 0;
    uint64_t concealment_events = 0;
    uint64_t inserted_samples_for_deceleration = 0;
    uint64_t removed_samples_for_acceleration = 0;
    // Instantaneous level in [0, 1]; the energy/duration pair is the
    // spec-correct way to average level over an interval.
    double audio_level = 0.0;
    double total_audio_energy = 0.0;
    double total_samples_duration_seconds = 0.0;
};

// Decode/render continuity counters for one received video track.
struct VideoReceiveStats {
    uint32_t frames_received = 0;
    // Instantaneous decoder output rate.
    double frames_per_second = 0.0;
    uint32_t frame_width = 0;
    uint32_t frame_height = 0;
    uint32_t freeze_count = 0;
    double total_freezes_duration_seconds = 0.0;
    uint32_t pause_count = 0;
    double total_pauses_duration_seconds = 0.0;
    double total_inter_frame_delay_seconds = 0.0;
    double total_squared_inter_frame_delay_seconds = 0.0;
    uint32_t pli_count = 0;
    uint32_t fir_count = 0;
    uint64_t qp_sum = 0;
};

} // namespace driscord::media
