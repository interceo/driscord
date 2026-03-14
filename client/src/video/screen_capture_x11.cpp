#include "screen_capture.hpp"

#if defined(__linux__)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "log.hpp"

class X11ScreenCapture : public ScreenCapture {
public:
    ~X11ScreenCapture() override { stop(); }

    bool start(int target_fps, int width, int height, FrameCallback cb) override {
        if (running_) return false;

        callback_ = std::move(cb);
        target_fps_ = std::max(1, std::min(target_fps, 120));
        target_w_ = (width > 0) ? width : 1920;
        target_h_ = (height > 0) ? height : 1080;

        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            LOG_ERROR() << "XOpenDisplay failed";
            return false;
        }

        root_ = DefaultRootWindow(display_);
        screen_ = DefaultScreen(display_);

        XWindowAttributes attrs;
        XGetWindowAttributes(display_, root_, &attrs);
        src_w_ = attrs.width;
        src_h_ = attrs.height;

        use_shm_ = XShmQueryExtension(display_) == True;

        if (use_shm_) {
            shm_image_ = XShmCreateImage(
                display_, DefaultVisual(display_, screen_),
                static_cast<unsigned>(DefaultDepth(display_, screen_)),
                ZPixmap, nullptr, &shm_info_,
                static_cast<unsigned>(src_w_), static_cast<unsigned>(src_h_));

            if (!shm_image_) {
                LOG_WARNING() << "XShmCreateImage failed, falling back to XGetImage";
                use_shm_ = false;
            } else {
                shm_info_.shmid = shmget(
                    IPC_PRIVATE,
                    static_cast<size_t>(shm_image_->bytes_per_line) * shm_image_->height,
                    IPC_CREAT | 0600);
                if (shm_info_.shmid < 0) {
                    LOG_WARNING() << "shmget failed, falling back to XGetImage";
                    XDestroyImage(shm_image_);
                    shm_image_ = nullptr;
                    use_shm_ = false;
                } else {
                    shm_info_.shmaddr = static_cast<char*>(shmat(shm_info_.shmid, nullptr, 0));
                    shm_image_->data = shm_info_.shmaddr;
                    shm_info_.readOnly = False;
                    XShmAttach(display_, &shm_info_);
                    XSync(display_, False);
                }
            }
        }

        running_ = true;
        thread_ = std::thread(&X11ScreenCapture::capture_loop, this);
        LOG_INFO() << "screen capture started (src " << src_w_ << "x" << src_h_
                   << " -> " << target_w_ << "x" << target_h_
                   << " @ " << target_fps_ << " fps"
                   << (use_shm_ ? ", XShm" : ", XGetImage") << ")";
        return true;
    }

    void stop() override {
        if (!running_) return;
        running_ = false;
        if (thread_.joinable()) thread_.join();

        cleanup_shm();
        if (display_) {
            XCloseDisplay(display_);
            display_ = nullptr;
        }
        LOG_INFO() << "screen capture stopped";
    }

    bool running() const override { return running_; }

private:
    void cleanup_shm() {
        if (shm_image_) {
            XShmDetach(display_, &shm_info_);
            shmdt(shm_info_.shmaddr);
            shmctl(shm_info_.shmid, IPC_RMID, nullptr);
            XDestroyImage(shm_image_);
            shm_image_ = nullptr;
        }
    }

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
        XImage* img = nullptr;

        if (use_shm_) {
            XShmGetImage(display_, root_, shm_image_, 0, 0, AllPlanes);
            img = shm_image_;
        } else {
            img = XGetImage(display_, root_, 0, 0,
                            static_cast<unsigned>(src_w_), static_cast<unsigned>(src_h_),
                            AllPlanes, ZPixmap);
            if (!img) return;
        }

        bool needs_scale = (src_w_ != target_w_ || src_h_ != target_h_);

        Frame frame;
        frame.width = target_w_;
        frame.height = target_h_;
        frame.data.resize(static_cast<size_t>(target_w_) * target_h_ * 4);

        if (needs_scale) {
            // Nearest-neighbour downscale: fast, good enough for screen content.
            for (int y = 0; y < target_h_; ++y) {
                int sy = y * src_h_ / target_h_;
                for (int x = 0; x < target_w_; ++x) {
                    int sx = x * src_w_ / target_w_;
                    unsigned long pixel = XGetPixel(img, sx, sy);
                    auto* dst = &frame.data[static_cast<size_t>((y * target_w_ + x) * 4)];
                    dst[0] = static_cast<uint8_t>(pixel & 0xFF);         // B
                    dst[1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);  // G
                    dst[2] = static_cast<uint8_t>((pixel >> 16) & 0xFF); // R
                    dst[3] = 255;                                         // A
                }
            }
        } else {
            // Direct copy — X11 ZPixmap with depth 24/32 is already BGRX.
            auto* src = reinterpret_cast<const uint8_t*>(img->data);
            int bpl = img->bytes_per_line;
            for (int y = 0; y < src_h_; ++y) {
                const auto* row = src + y * bpl;
                auto* dst = &frame.data[static_cast<size_t>(y * target_w_ * 4)];
                for (int x = 0; x < src_w_; ++x) {
                    dst[x * 4 + 0] = row[x * 4 + 0]; // B
                    dst[x * 4 + 1] = row[x * 4 + 1]; // G
                    dst[x * 4 + 2] = row[x * 4 + 2]; // R
                    dst[x * 4 + 3] = 255;              // A
                }
            }
        }

        if (!use_shm_) {
            XDestroyImage(img);
        }

        if (callback_) callback_(frame);
    }

    std::atomic<bool> running_{false};
    int target_fps_ = 60;
    int target_w_ = 1920;
    int target_h_ = 1080;
    int src_w_ = 0;
    int src_h_ = 0;
    FrameCallback callback_;
    std::thread thread_;

    Display* display_ = nullptr;
    Window root_{};
    int screen_ = 0;

    bool use_shm_ = false;
    XShmSegmentInfo shm_info_{};
    XImage* shm_image_ = nullptr;
};

std::unique_ptr<ScreenCapture> ScreenCapture::create() {
    return std::make_unique<X11ScreenCapture>();
}

#endif  // __linux__
