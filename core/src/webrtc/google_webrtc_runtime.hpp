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

struct InjectedAudioDeviceConfig {
    using RenderCallback = std::function<void(
        std::span<const int16_t> samples, int sample_rate_hz, size_t channels)>;

    int sample_rate_hz = 48'000;
    size_t channels = 1;
    size_t max_buffered_frames = 500;
    RenderCallback on_rendered_audio;
};

struct IceServerConfig {
    std::string url;
    std::string username;
    std::string password;
};

struct GoogleWebRtcRuntimeConfig {
    std::optional<InjectedAudioDeviceConfig> injected_audio_device;
    std::vector<IceServerConfig> ice_servers;
};

struct AudioDeviceInfo {
    std::string id;
    std::string name;
};

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

    [[nodiscard]] bool audio_device_degraded() const noexcept;

    [[nodiscard]] bool submit_recorded_audio_10ms(
        std::span<const int16_t> interleaved_samples);

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

}
