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
    std::span<const int16_t> samples;
    int sample_rate_hz = 0;
    size_t channels = 0;
    size_t frames = 0;
};

enum class QualityLimitation {
    None,
    Cpu,
    Bandwidth,
    Other,
};

struct RtpReceiveStats {
    double jitter_seconds = 0.0;
    uint32_t nack_count = 0;
    uint64_t packets_discarded = 0;
    uint64_t retransmitted_packets_received = 0;
    double estimated_playout_timestamp_ms = -1.0;
};

struct AudioReceiveStats {
    uint64_t total_samples_received = 0;
    uint64_t silent_concealed_samples = 0;
    uint64_t concealment_events = 0;
    uint64_t inserted_samples_for_deceleration = 0;
    uint64_t removed_samples_for_acceleration = 0;
    double audio_level = 0.0;
    double total_audio_energy = 0.0;
    double total_samples_duration_seconds = 0.0;
};

struct VideoReceiveStats {
    uint32_t frames_received = 0;
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

}
