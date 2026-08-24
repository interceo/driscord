#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace driscord::media {

class GoogleWebRtcVoiceSession;
class GoogleWebRtcScreenSession;

// Replaces the platform audio device with an in-process, clocked PCM source.
// It is primarily intended for deterministic integration tests and headless
// deployments. Each submitted buffer must contain exactly 10 ms of
// interleaved signed 16-bit PCM. Decoded remote tracks are still exposed via
// VoiceSessionCallbacks::on_remote_audio.
struct InjectedAudioDeviceConfig {
    using RenderCallback = std::function<void(
        std::span<const int16_t> samples, int sample_rate_hz, size_t channels)>;

    int sample_rate_hz = 48'000;
    size_t channels = 1;
    size_t max_buffered_frames = 500;
    // Receives WebRTC's mixed 10-ms playout frames. The span is borrowed and
    // must be copied before the callback returns. Empty keeps headless tests
    // allocation-free by using WebRTC's discard renderer.
    RenderCallback on_rendered_audio;
};

// One STUN or TURN server, in the form the user configures it. Kept as a plain
// value type so the setting can travel from the Qt config to both sessions
// without a WebRTC type appearing outside this layer.
struct IceServerConfig {
    std::string url;
    std::string username;
    std::string password;
};

struct GoogleWebRtcRuntimeConfig {
    std::optional<InjectedAudioDeviceConfig> injected_audio_device;
    // Empty means host candidates only, which reaches an SFU just in the case
    // where a publicly routable address sits directly on its interface.
    std::vector<IceServerConfig> ice_servers;
};

struct AudioDeviceInfo {
    std::string id;
    std::string name;
};

// Process-level resources required by all WebRTC PeerConnections.  The class
// exists to make thread/factory teardown deterministic; track senders,
// receivers and sinks are intentionally not mirrored by wrapper classes.
class GoogleWebRtcRuntime final {
public:
    GoogleWebRtcRuntime();
    explicit GoogleWebRtcRuntime(GoogleWebRtcRuntimeConfig config);
    ~GoogleWebRtcRuntime();

    GoogleWebRtcRuntime(const GoogleWebRtcRuntime&) = delete;
    GoogleWebRtcRuntime& operator=(const GoogleWebRtcRuntime&) = delete;
    GoogleWebRtcRuntime(GoogleWebRtcRuntime&&) noexcept;
    GoogleWebRtcRuntime& operator=(GoogleWebRtcRuntime&&) noexcept;

    [[nodiscard]] bool ready() const noexcept;

    // True when no working platform audio device was available at
    // construction and the runtime fell back to WebRTC's silent dummy
    // device: sessions still connect and negotiate audio, but nothing is
    // captured or played out. Always false with an injected audio device.
    [[nodiscard]] bool audio_device_degraded() const noexcept;

    // Queues one 10 ms frame for an injected audio device. Returns false when
    // the runtime uses a platform device, the shape is invalid, or the bounded
    // queue is full. One runtime represents one physical/logical microphone;
    // use separate runtimes for independent test participants.
    [[nodiscard]] bool submit_recorded_audio_10ms(
        std::span<const int16_t> interleaved_samples);

    // These methods address the native ADM used by this runtime. Device IDs
    // are opaque and stable across re-enumeration whenever the platform
    // exposes a GUID; Linux/PulseAudio currently falls back to the display
    // name because upstream leaves the GUID empty.
    [[nodiscard]] std::vector<AudioDeviceInfo> recording_devices() const;
    [[nodiscard]] std::vector<AudioDeviceInfo> playout_devices() const;
    [[nodiscard]] bool set_recording_device(std::string_view id);
    [[nodiscard]] bool set_playout_device(std::string_view id);

private:
    friend class GoogleWebRtcScreenSession;
    friend class GoogleWebRtcVoiceSession;

    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace driscord::media
