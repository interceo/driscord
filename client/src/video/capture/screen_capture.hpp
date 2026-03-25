#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct CaptureTarget {
    enum Type { Monitor, Window };

    Type type = Monitor;
    std::string id;    // window id (X11 decimal) or screen index (macOS)
    std::string name;  // human-readable label
    int width = 0;
    int height = 0;
    int x = 0;  // screen offset for monitor region capture
    int y = 0;

    static CaptureTarget from_json(const nlohmann::json& j) {
        CaptureTarget t;
        t.type   = j.value("type", 0) == 0 ? Monitor : Window;
        t.id     = j.value("id",     "");
        t.name   = j.value("name",   "");
        t.width  = j.value("width",  0);
        t.height = j.value("height", 0);
        t.x      = j.value("x",      0);
        t.y      = j.value("y",      0);
        return t;
    }
};

class ScreenCapture {
public:
    struct Frame {
        std::vector<uint8_t> data;  // BGRA pixel data
        int width = 0;
        int height = 0;
        std::chrono::system_clock::time_point capture_ts{};

        // Returns pixel data with B and R channels swapped (RGBA).
        std::vector<uint8_t> to_rgba() const {
            auto out = data;
            for (size_t i = 0; i + 3 < out.size(); i += 4)
                std::swap(out[i], out[i + 2]);
            return out;
        }
    };

    using FrameCallback = std::function<void(Frame frame)>;

    static std::unique_ptr<ScreenCapture> create();
    static std::vector<CaptureTarget> list_targets();
    static Frame grab_thumbnail(const CaptureTarget& target, int max_w, int max_h);

    virtual ~ScreenCapture() = default;
    virtual bool start(int fps, const CaptureTarget& target, int max_w, int max_h, FrameCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};
