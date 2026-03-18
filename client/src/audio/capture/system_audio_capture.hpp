#pragma once

#include "utils/opus_codec.hpp"

#include <cstddef>
#include <functional>
#include <memory>

class SystemAudioCapture {
public:
    static constexpr int kChannels = 2;
    using AudioCallback            = std::function<void(const float* samples, size_t frame_count, int channels)>;

    static std::unique_ptr<SystemAudioCapture> create();
    static bool available();

    virtual ~SystemAudioCapture()        = default;
    virtual bool start(AudioCallback cb) = 0;
    virtual void stop()                  = 0;
    virtual bool running() const         = 0;
};
