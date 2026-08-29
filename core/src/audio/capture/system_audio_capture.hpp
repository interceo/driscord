#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct AudioCaptureTarget {
    std::string id;
    std::string name;

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

    static std::vector<AudioCaptureTarget> list_sinks();

    virtual ~SystemAudioCapture() = default;
    virtual bool start(const std::string& target_id, AudioCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};
