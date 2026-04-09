#pragma once

#include "opus_codec.hpp"

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
    static constexpr int kChannels = 2;
    using AudioCallback = std::function<
        void(const float* samples, size_t frame_count, int channels)>;

    static std::unique_ptr<SystemAudioCapture> create();
    static bool available();

    // PA sinks (playback devices) whose monitors can be captured for loopback.
    static std::vector<AudioCaptureTarget> list_sinks();

    // PA sources that are physical inputs (microphones), excluding sink monitors.
    static std::vector<AudioCaptureTarget> list_sources();

    virtual ~SystemAudioCapture() = default;
    virtual bool start(AudioCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};
