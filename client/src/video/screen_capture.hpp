#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct CaptureTarget {
    std::string id;    // window id (X11 decimal) or screen index (macOS)
    std::string name;  // human-readable label
    int width = 0;
    int height = 0;
};

class ScreenCapture {
public:
    struct Frame {
        std::vector<uint8_t> data;  // BGRA pixel data
        int width = 0;
        int height = 0;
    };

    using FrameCallback = std::function<void(const Frame&)>;

    static std::unique_ptr<ScreenCapture> create();
    static std::vector<CaptureTarget> list_targets();

    virtual ~ScreenCapture() = default;
    virtual bool start(int fps, const CaptureTarget& target, int max_w, int max_h, FrameCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};
