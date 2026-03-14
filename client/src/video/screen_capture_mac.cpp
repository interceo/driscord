#include "screen_capture.hpp"

#ifdef __APPLE__

#include <CoreGraphics/CoreGraphics.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include "log.hpp"

namespace {

constexpr int kCaptureWidth = 1920;
constexpr int kCaptureHeight = 1080;

}  // namespace

class MacScreenCapture : public ScreenCapture {
public:
    ~MacScreenCapture() override { stop(); }

    bool start(int target_fps, FrameCallback cb) override {
        if (running_) return false;
        callback_ = std::move(cb);
        target_fps_ = std::max(1, std::min(target_fps, 60));
        running_ = true;
        thread_ = std::thread(&MacScreenCapture::capture_loop, this);
        LOG_INFO() << "screen capture started (" << kCaptureWidth << "x" << kCaptureHeight
                   << " @ " << target_fps_ << " fps)";
        return true;
    }

    void stop() override {
        if (!running_) return;
        running_ = false;
        if (thread_.joinable()) thread_.join();
        LOG_INFO() << "screen capture stopped";
    }

    bool running() const override { return running_; }

private:
    void capture_loop() {
        auto interval = std::chrono::microseconds(1'000'000 / target_fps_);

        while (running_) {
            auto t0 = std::chrono::steady_clock::now();
            capture_frame();
            auto elapsed = std::chrono::steady_clock::now() - t0;
            if (elapsed < interval) {
                std::this_thread::sleep_for(interval - elapsed);
            }
        }
    }

    void capture_frame() {
        CGImageRef image = CGDisplayCreateImage(CGMainDisplayID());
        if (!image) return;

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        // BGRA in memory (little-endian ARGB)
        CGContextRef ctx = CGBitmapContextCreate(
            nullptr, kCaptureWidth, kCaptureHeight, 8, kCaptureWidth * 4, cs,
            kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
        CGColorSpaceRelease(cs);

        if (!ctx) {
            CGImageRelease(image);
            return;
        }

        CGContextSetInterpolationQuality(ctx, kCGInterpolationMedium);
        CGContextDrawImage(ctx, CGRectMake(0, 0, kCaptureWidth, kCaptureHeight), image);

        auto* pixels = static_cast<const uint8_t*>(CGBitmapContextGetData(ctx));
        std::size_t nbytes = static_cast<std::size_t>(kCaptureWidth) * kCaptureHeight * 4;

        Frame frame;
        frame.width = kCaptureWidth;
        frame.height = kCaptureHeight;
        frame.data.assign(pixels, pixels + nbytes);

        CGContextRelease(ctx);
        CGImageRelease(image);

        if (callback_) callback_(frame);
    }

    std::atomic<bool> running_{false};
    int target_fps_ = 15;
    FrameCallback callback_;
    std::thread thread_;
};

std::unique_ptr<ScreenCapture> ScreenCapture::create() {
    return std::make_unique<MacScreenCapture>();
}

#endif  // __APPLE__
