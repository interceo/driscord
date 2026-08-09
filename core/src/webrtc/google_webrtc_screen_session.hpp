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

enum class DesktopCaptureKind {
    Screen,
    Window,
};

struct DesktopCaptureSource {
    int64_t id = 0;
    std::string title;
};

struct DesktopCaptureThumbnail {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};

struct ScreenSessionConfig {
    // Each slot is a recvonly video/audio pair. Both tracks retain their mids
    // when the SFU reassigns a different publisher to that slot.
    size_t remote_stream_slots = 4;
    bool sharing_enabled = false;
    bool system_audio_enabled = false;
    int max_video_bitrate_bps = 4'000'000;
    int max_system_audio_bitrate_bps = 128'000;
};

struct DecodedVideoFrameView {
    // RGBA, tightly packed, valid only for the duration of on_remote_video.
    std::span<const uint8_t> rgba;
    int width = 0;
    int height = 0;
    int64_t timestamp_us = 0;
};

struct ScreenInboundRtpStats {
    std::string mid;
    bool video = false;
    uint64_t packets_received = 0;
    uint64_t bytes_received = 0;
    int64_t packets_lost = 0;
    uint64_t jitter_buffer_emitted_count = 0;
    double jitter_buffer_delay_seconds = 0.0;
    double jitter_buffer_target_delay_seconds = 0.0;
    uint64_t concealed_samples = 0;
    uint32_t frames_decoded = 0;
    uint32_t frames_dropped = 0;
    uint32_t key_frames_decoded = 0;
};

struct ScreenSessionStats {
    uint64_t video_packets_sent = 0;
    uint64_t video_bytes_sent = 0;
    uint64_t audio_packets_sent = 0;
    uint64_t audio_bytes_sent = 0;
    std::vector<ScreenInboundRtpStats> inbound;
};

struct ScreenSessionCallbacks {
    std::function<void(std::string sdp)> on_offer;
    std::function<void(std::string candidate, std::string mid)> on_candidate;
    std::function<void(std::string mid,
        std::string track_id,
        bool video)>
        on_remote_track;
    std::function<void(std::string_view mid, DecodedVideoFrameView frame)>
        on_remote_video;
    std::function<void(std::string_view mid, DecodedAudioFrameView frame)>
        on_remote_audio;
    std::function<void(ScreenConnectionState)> on_state;
    std::function<void(std::string message)> on_error;
};

// Owns one screen PeerConnection: a local screen-video/system-audio pair and a
// bounded pool of recvonly pairs. Capture frames enter a native WebRTC
// VideoTrackSource; encoded packetization, jitter buffering and decode remain
// Google WebRTC responsibilities.
class GoogleWebRtcScreenSession final {
public:
    static constexpr size_t kMaxRemoteStreamSlots = 16;

    GoogleWebRtcScreenSession(GoogleWebRtcRuntime& runtime,
        ScreenSessionConfig config,
        ScreenSessionCallbacks callbacks);
    ~GoogleWebRtcScreenSession();

    GoogleWebRtcScreenSession(const GoogleWebRtcScreenSession&) = delete;
    GoogleWebRtcScreenSession& operator=(const GoogleWebRtcScreenSession&) = delete;
    GoogleWebRtcScreenSession(GoogleWebRtcScreenSession&&) noexcept;
    GoogleWebRtcScreenSession& operator=(GoogleWebRtcScreenSession&&) noexcept;

    bool start();
    void close() noexcept;
    void apply_answer(std::string_view sdp);
    void add_remote_candidate(std::string_view candidate, std::string_view mid);

    void set_sharing_enabled(bool enabled);
    void set_system_audio_enabled(bool enabled);
    [[nodiscard]] bool sharing_enabled() const noexcept;
    [[nodiscard]] size_t remote_stream_slots() const noexcept;
    bool set_remote_audio_enabled(std::string_view mid, bool enabled);
    bool set_remote_audio_volume(std::string_view mid, double volume);
    bool get_stats(std::function<void(ScreenSessionStats)> callback);

    // Push path used by deterministic tests and by platform capture bridges.
    // The source copies/converts the frame before returning.
    [[nodiscard]] bool submit_bgra_frame(std::span<const uint8_t> bgra,
        int width,
        int height,
        int stride,
        int64_t timestamp_us);

    // Native Google WebRTC DesktopCapturer path. Enumeration is synchronous;
    // capture itself runs on a dedicated stoppable thread.
    [[nodiscard]] std::vector<DesktopCaptureSource> desktop_sources(
        DesktopCaptureKind kind) const;
    [[nodiscard]] static std::vector<DesktopCaptureSource>
    list_desktop_sources(DesktopCaptureKind kind);
    [[nodiscard]] static DesktopCaptureThumbnail grab_desktop_thumbnail(
        DesktopCaptureKind kind,
        int64_t source_id,
        int max_width,
        int max_height);
    [[nodiscard]] bool start_desktop_capture(DesktopCaptureKind kind,
        int64_t source_id,
        int max_fps = 30,
        int max_width = 0,
        int max_height = 0);
    void stop_desktop_capture() noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace driscord::media
