#pragma once

#include "webrtc/google_webrtc_media_types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace driscord::media {

class GoogleWebRtcRuntime;

struct VoiceSessionConfig {
    size_t remote_track_slots = 16;
    bool microphone_enabled = true;
    int max_microphone_bitrate_bps = 64'000;
};

struct VoiceInboundRtpStats {
    std::string mid;
    uint64_t packets_received = 0;
    uint64_t bytes_received = 0;
    int64_t packets_lost = 0;
    uint64_t concealed_samples = 0;
    uint64_t jitter_buffer_emitted_count = 0;
    double jitter_buffer_delay_seconds = 0.0;
    double jitter_buffer_target_delay_seconds = 0.0;
    RtpReceiveStats rtp;
    AudioReceiveStats audio;
};

struct VoiceSessionStats {
    uint64_t packets_sent = 0;
    uint64_t bytes_sent = 0;
    double round_trip_time_seconds = -1.0;
    double available_outgoing_bitrate_bps = -1.0;
    std::vector<VoiceInboundRtpStats> inbound;
};

struct VoiceSessionCallbacks {
    std::function<void(std::string sdp)> on_offer;
    std::function<void(std::string candidate, std::string mid)> on_candidate;
    std::function<void(std::string mid, std::string track_id)> on_remote_track;
    std::function<void(std::string_view mid, DecodedAudioFrameView frame)>
        on_remote_audio;
    std::function<void(VoiceConnectionState)> on_state;
    std::function<void(std::string message)> on_error;
};

class GoogleWebRtcVoiceSession final {
public:
    static constexpr size_t kMaxRemoteTrackSlots = 64;

    GoogleWebRtcVoiceSession(GoogleWebRtcRuntime& runtime,
        VoiceSessionConfig config,
        VoiceSessionCallbacks callbacks);
    ~GoogleWebRtcVoiceSession();

    GoogleWebRtcVoiceSession(const GoogleWebRtcVoiceSession&) = delete;
    GoogleWebRtcVoiceSession& operator=(const GoogleWebRtcVoiceSession&) = delete;
    GoogleWebRtcVoiceSession(GoogleWebRtcVoiceSession&&) noexcept;
    GoogleWebRtcVoiceSession& operator=(GoogleWebRtcVoiceSession&&) noexcept;

    bool start();
    void close() noexcept;

    void apply_answer(std::string_view sdp);
    void add_remote_candidate(std::string_view candidate, std::string_view mid);

    void set_microphone_enabled(bool enabled);
    [[nodiscard]] bool microphone_enabled() const noexcept;
    [[nodiscard]] size_t remote_track_slots() const noexcept;

    bool set_remote_track_enabled(std::string_view mid, bool enabled);
    bool set_remote_track_volume(std::string_view mid, double volume);

    bool get_stats(std::function<void(VoiceSessionStats)> callback);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}
