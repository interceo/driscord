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
    // Pre-negotiated Unified Plan recvonly m-lines. Each remote voice is bound
    // to one slot by its mid; the count is intentionally bounded.
    size_t remote_track_slots = 16;
    bool microphone_enabled = true;
    // Conservative Opus ceiling. The SFU terminates RTCP per hop, so a bounded
    // publisher prevents one fan-out stream from growing without useful
    // downstream transport-wide feedback.
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
};

struct VoiceSessionStats {
    uint64_t packets_sent = 0;
    uint64_t bytes_sent = 0;
    std::vector<VoiceInboundRtpStats> inbound;
};

struct VoiceSessionCallbacks {
    // Callbacks run on WebRTC's signaling/network threads. Consumers that
    // touch UI state must marshal them onto their own executor.
    std::function<void(std::string sdp)> on_offer;
    std::function<void(std::string candidate, std::string mid)> on_candidate;
    std::function<void(std::string mid, std::string track_id)> on_remote_track;
    // Already decoded, post-NetEq PCM for each independently negotiated mid.
    // The view must not be retained after the callback returns.
    std::function<void(std::string_view mid, DecodedAudioFrameView frame)>
        on_remote_audio;
    std::function<void(VoiceConnectionState)> on_state;
    std::function<void(std::string message)> on_error;
};

// Owns one voice PeerConnection: one sendonly microphone track and a bounded
// pool of recvonly tracks. Encoding, NetEq, mixing and playout are native
// Google WebRTC responsibilities; this class only coordinates lifecycle and
// signaling, so no AudioSender/AudioReceiver wrappers are reintroduced.
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

    // Starts negotiation and emits exactly one initial offer on success.
    // Returns false only for an immediate configuration/factory error. The
    // object is intentionally one-shot: create a new session after close().
    bool start();
    void close() noexcept;

    void apply_answer(std::string_view sdp);
    void add_remote_candidate(std::string_view candidate, std::string_view mid);

    void set_microphone_enabled(bool enabled);
    [[nodiscard]] bool microphone_enabled() const noexcept;
    [[nodiscard]] size_t remote_track_slots() const noexcept;

    // Per-track controls operate on a negotiated recvonly mid. They return
    // false when the mid is unknown or is not an audio receiver.
    bool set_remote_track_enabled(std::string_view mid, bool enabled);
    bool set_remote_track_volume(std::string_view mid, double volume);

    // Delivers a spec-compliant snapshot asynchronously on WebRTC's stats
    // thread. Returns false if the session has not started or is closed.
    bool get_stats(std::function<void(VoiceSessionStats)> callback);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace driscord::media
