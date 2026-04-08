#include "screen_capture.hpp"
#include "screen_capture_common.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "log.hpp"

#include <CoreGraphics/CoreGraphics.h>

static void check_screen_recording_permission()
{
    if (!CGPreflightScreenCaptureAccess()) {
        LOG_WARNING() << "screen recording permission not granted — "
                         "grant it in System Settings > Privacy & Security > "
                         "Screen Recording, "
                         "then RESTART the app";
        CGRequestScreenCaptureAccess();
    }
}

// --- target enumeration -----------------------------------------------------

std::vector<ScreenCaptureTarget> ScreenCapture::list_targets()
{
    check_screen_recording_permission();
    std::vector<ScreenCaptureTarget> targets;

    CGDirectDisplayID displays[16];
    uint32_t count = 0;
    CGGetActiveDisplayList(16, displays, &count);

    targets.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto bounds = CGDisplayBounds(displays[i]);
        int w = static_cast<int>(bounds.size.width);
        int h = static_cast<int>(bounds.size.height);

        ScreenCaptureTarget t;
        t.type = ScreenCaptureTarget::Monitor;
        t.id = std::to_string(i);
        t.name = "Display " + std::to_string(i + 1) + " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
        t.width = w;
        t.height = h;
        targets.emplace_back(std::move(t));
    }
    return targets;
}

// --- helpers ----------------------------------------------------------------

static CGDirectDisplayID resolve_display(const std::string& id)
{
    CGDirectDisplayID displays[16];
    uint32_t count = 0;
    CGGetActiveDisplayList(16, displays, &count);
    uint32_t idx = 0;
    if (!id.empty()) {
        try {
            idx = static_cast<uint32_t>(std::stoul(id));
        } catch (const std::exception&) {
            return CGMainDisplayID();
        }
    }
    return (idx < count) ? displays[idx] : CGMainDisplayID();
}

// --- thumbnail --------------------------------------------------------------

ScreenCapture::Frame ScreenCapture::grab_thumbnail(
    const ScreenCaptureTarget& target,
    int max_w,
    int max_h)
{
    Frame f;
    CGDirectDisplayID did = resolve_display(target.id);
    CGImageRef image = CGDisplayCreateImage(did);
    if (!image) {
        return f;
    }

    int src_w = static_cast<int>(CGImageGetWidth(image));
    int src_h = static_cast<int>(CGImageGetHeight(image));

    int ow, oh;
    compute_output_size(src_w, src_h, max_w, max_h, ow, oh);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    f.width = ow;
    f.height = oh;
    f.data.resize(static_cast<size_t>(ow) * oh * 4);

    CGContextRef ctx = CGBitmapContextCreate(
        f.data.data(), ow, oh, 8, ow * 4, cs,
        static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst));
    CGContextDrawImage(ctx, CGRectMake(0, 0, ow, oh), image);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    CGImageRelease(image);
    return f;
}

// --- capture implementation -------------------------------------------------

class MacScreenCapture : public ScreenCapture {
public:
    ~MacScreenCapture() override { stop(); }

    bool start(int fps,
        const ScreenCaptureTarget& target,
        int max_w,
        int max_h,
        FrameCallback cb) override
    {
        if (running_) {
            return false;
        }

        check_screen_recording_permission();

        callback_ = std::move(cb);
        max_w_ = max_w;
        max_h_ = max_h;
        display_id_ = resolve_display(target.id);
        frame_interval_us_ = 1000000 / std::max(fps, 1);

        running_ = true;
        thread_ = std::thread(&MacScreenCapture::capture_loop, this);
        LOG_INFO() << "screen capture started (CoreGraphics) @ " << fps << " fps";
        return true;
    }

    void stop() override
    {
        if (!running_.exchange(false)) {
            return;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        release_cached_cs();
        LOG_INFO() << "screen capture stopped";
    }

    bool running() const override { return running_; }

private:
    void capture_loop()
    {
        while (running_) {
            auto t0 = std::chrono::steady_clock::now();

            CGImageRef image = CGDisplayCreateImage(display_id_);
            if (image && running_) {
                deliver_frame(image);
                CGImageRelease(image);
            }

            auto elapsed = std::chrono::steady_clock::now() - t0;
            auto target_dur = std::chrono::microseconds(frame_interval_us_);
            if (elapsed < target_dur) {
                std::this_thread::sleep_for(target_dur - elapsed);
            }
        }
    }

    void deliver_frame(CGImageRef image)
    {
        int src_w = static_cast<int>(CGImageGetWidth(image));
        int src_h = static_cast<int>(CGImageGetHeight(image));

        int ow, oh;
        compute_output_size(src_w, src_h, max_w_, max_h_, ow, oh);

        if (!cached_cs_) {
            cached_cs_ = CGColorSpaceCreateDeviceRGB();
        }

        Frame out;
        out.width = ow;
        out.height = oh;
        out.data.resize(static_cast<size_t>(ow) * oh * 4);

        CGContextRef ctx = CGBitmapContextCreate(
            out.data.data(), ow, oh, 8, ow * 4, cached_cs_,
            static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst));
        CGContextDrawImage(ctx, CGRectMake(0, 0, ow, oh), image);
        CGContextRelease(ctx);

        if (callback_ && running_) {
            callback_(std::move(out));
        }
    }

    void release_cached_cs()
    {
        if (cached_cs_) {
            CGColorSpaceRelease(cached_cs_);
            cached_cs_ = nullptr;
        }
    }

    std::atomic<bool> running_ { false };
    FrameCallback callback_;
    std::thread thread_;
    int max_w_ = 1920;
    int max_h_ = 1080;

    CGDirectDisplayID display_id_ = 0;
    int frame_interval_us_ = 33333;

    CGColorSpaceRef cached_cs_ = nullptr;
};

std::unique_ptr<ScreenCapture> ScreenCapture::create()
{
    return std::make_unique<MacScreenCapture>();
}
