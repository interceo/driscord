#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class ScreenCapture {
public:
    struct Frame {
        std::vector<uint8_t> data;  // BGRA pixel data
        int width = 0;
        int height = 0;
    };

    using FrameCallback = std::function<void(const Frame&)>;

    static std::unique_ptr<ScreenCapture> create();

    virtual ~ScreenCapture() = default;
    virtual bool start(int target_fps, FrameCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};
