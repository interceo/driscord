#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct AudioCaptureTarget {
    std::string id; // device identifier (e.g. ALSA device name)
    std::string name; // audio device name

    static AudioCaptureTarget from_json(const nlohmann::json& j)
    {
        AudioCaptureTarget t;
        t.id = j.value("id", "");
        t.name = j.value("name", "");
        return t;
    }
};

class SystemAudioCapture {
public:
    static constexpr int kSampleRate = 48'000;
    static constexpr int kChannels = 2;
    static constexpr size_t kFramesPerRead = 960;
    using AudioCallback = std::function<
        void(const float* samples, size_t frame_count, int channels)>;

    static std::unique_ptr<SystemAudioCapture> create();
    static bool available();

    // PA sinks (playback devices) whose monitors can be captured for loopback.
    static std::vector<AudioCaptureTarget> list_sinks();

    virtual ~SystemAudioCapture() = default;
    // `target_id` selects which playback device's monitor is captured; empty
    // means the current default sink. Capturing the monitor of the device the
    // user listens through also captures everyone else in the call, so the
    // choice belongs to the user.
    virtual bool start(const std::string& target_id, AudioCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};
