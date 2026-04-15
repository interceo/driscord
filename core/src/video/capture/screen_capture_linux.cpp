extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "screen_capture.hpp"
#include "screen_capture_common.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "log.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

// --- target enumeration -----------------------------------------------------

static std::string x11_window_name(Display* dpy, Window win)
{
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);

    Atom type { };
    int format { };
    unsigned long nitems { }, after { };
    unsigned char* data = nullptr;

    if (XGetWindowProperty(dpy, win, net_wm_name, 0, 256, False, utf8, &type,
            &format, &nitems, &after, &data)
            == Success
        && data && nitems > 0) {
        std::string name(reinterpret_cast<char*>(data), nitems);
        XFree(data);
        return name;
    }

    char* wm_name = nullptr;
    if (XFetchName(dpy, win, &wm_name) && wm_name) {
        std::string name(wm_name);
        XFree(wm_name);
        return name;
    }
    return { };
}

std::vector<ScreenCaptureTarget> ScreenCapture::list_targets()
{
    std::vector<ScreenCaptureTarget> targets;

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        return targets;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    XRRScreenResources* res = XRRGetScreenResources(dpy, root);
    if (res) {
        targets.reserve(res->ncrtc);
        for (int i = 0; i < res->ncrtc; ++i) {
            XRRCrtcInfo* crtc = XRRGetCrtcInfo(dpy, res, res->crtcs[i]);
            if (!crtc) {
                continue;
            }
            if (crtc->mode == None || crtc->width == 0 || crtc->height == 0) {
                XRRFreeCrtcInfo(crtc);
                continue;
            }

            std::string name = "Monitor " + std::to_string(i + 1);
            if (crtc->noutput > 0) {
                XRROutputInfo* output = XRRGetOutputInfo(dpy, res, crtc->outputs[0]);
                if (output) {
                    name = std::string(output->name, static_cast<size_t>(output->nameLen));
                    XRRFreeOutputInfo(output);
                }
            }
            name += " (" + std::to_string(crtc->width) + "x" + std::to_string(crtc->height) + ")";

            ScreenCaptureTarget t;
            t.type = ScreenCaptureTarget::Monitor;
            t.id = std::to_string(i);
            t.name = std::move(name);
            t.width = static_cast<int>(crtc->width);
            t.height = static_cast<int>(crtc->height);
            t.x = crtc->x;
            t.y = crtc->y;
            targets.emplace_back(std::move(t));

            XRRFreeCrtcInfo(crtc);
        }
        XRRFreeScreenResources(res);
    }

    if (targets.empty()) {
        XWindowAttributes root_attrs { };
        XGetWindowAttributes(dpy, root, &root_attrs);
        ScreenCaptureTarget t;
        t.type = ScreenCaptureTarget::Monitor;
        t.id = "0";
        t.name = "Full Desktop (" + std::to_string(root_attrs.width) + "x" + std::to_string(root_attrs.height) + ")";
        t.width = root_attrs.width;
        t.height = root_attrs.height;
        targets.emplace_back(std::move(t));
    }

    Atom net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    Atom type { };
    int format { };
    unsigned long nitems { }, after { };
    unsigned char* data = nullptr;

    if (XGetWindowProperty(dpy, root, net_client_list, 0, ~0L, False, XA_WINDOW,
            &type, &format, &nitems, &after, &data)
            == Success
        && data) {
        auto* windows = reinterpret_cast<Window*>(data);
        for (unsigned long i = 0; i < nitems; ++i) {
            XWindowAttributes attrs { };
            if (!XGetWindowAttributes(dpy, windows[i], &attrs)) {
                continue;
            }
            if (attrs.map_state != IsViewable) {
                continue;
            }
            if (attrs.width < 50 || attrs.height < 50) {
                continue;
            }

            std::string name = x11_window_name(dpy, windows[i]);
            if (name.empty()) {
                continue;
            }

            name += " (" + std::to_string(attrs.width) + "x" + std::to_string(attrs.height) + ")";

            ScreenCaptureTarget t;
            t.type = ScreenCaptureTarget::Window;
            t.id = std::to_string(static_cast<unsigned long>(windows[i]));
            t.name = std::move(name);
            t.width = attrs.width;
            t.height = attrs.height;
            targets.emplace_back(std::move(t));
        }
        XFree(data);
    }

    XCloseDisplay(dpy);
    return targets;
}

// --- thumbnail --------------------------------------------------------------

ScreenCapture::Frame ScreenCapture::grab_thumbnail(
    const ScreenCaptureTarget& target,
    int max_w,
    int max_h)
{
    Frame f;
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        return f;
    }

    int scr = DefaultScreen(dpy);
    Window root = RootWindow(dpy, scr);

    int src_x = 0, src_y = 0, src_w = 0, src_h = 0;
    Window grab_win = root;

    if (target.type == ScreenCaptureTarget::Window && !target.id.empty()) {
        try {
            grab_win = static_cast<Window>(std::stoul(target.id));
        } catch (const std::exception&) {
            XCloseDisplay(dpy);
            return f;
        }
        XWindowAttributes attrs { };
        if (!XGetWindowAttributes(dpy, grab_win, &attrs)) {
            XCloseDisplay(dpy);
            return f;
        }
        src_w = attrs.width;
        src_h = attrs.height;
    } else {
        src_x = target.x;
        src_y = target.y;
        src_w = target.width;
        src_h = target.height;
        grab_win = root;
    }

    if (src_w <= 0 || src_h <= 0) {
        XCloseDisplay(dpy);
        return f;
    }

    XImage* img = XGetImage(dpy, grab_win, src_x, src_y, static_cast<unsigned>(src_w),
        static_cast<unsigned>(src_h), AllPlanes, ZPixmap);
    XCloseDisplay(dpy);
    if (!img) {
        return f;
    }

    std::vector<uint8_t> bgra(static_cast<size_t>(src_w) * src_h * 4);
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w; ++x) {
            unsigned long pixel = XGetPixel(img, x, y);
            size_t idx = (static_cast<size_t>(y) * src_w + x) * 4;
            bgra[idx + 0] = static_cast<uint8_t>(pixel & 0xFF);
            bgra[idx + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);
            bgra[idx + 2] = static_cast<uint8_t>((pixel >> 16) & 0xFF);
            bgra[idx + 3] = 255;
        }
    }
    XDestroyImage(img);

    int ow, oh;
    compute_output_size(src_w, src_h, max_w, max_h, ow, oh);

    f.width = ow;
    f.height = oh;
    f.data.resize(static_cast<size_t>(ow) * oh * 4);

    if (ow == src_w && oh == src_h) {
        f.data = std::move(bgra);
    } else {
        scale_nearest(bgra.data(), src_w, src_h, f.data.data(), ow, oh);
    }
    return f;
}

// --- FFmpeg x11grab capture -------------------------------------------------

class LinuxScreenCapture : public ScreenCapture {
public:
    ~LinuxScreenCapture() override { stop(); }

    bool start(int fps,
        const ScreenCaptureTarget& target,
        int max_w,
        int max_h,
        FrameCallback cb) override
    {
        if (running_) {
            return false;
        }

        callback_ = std::move(cb);
        max_w_ = max_w;
        max_h_ = max_h;

        avdevice_register_all();

        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "framerate", std::to_string(fps).c_str(), 0);
        av_dict_set(&opts, "probesize", "5000000", 0);
        av_dict_set(&opts, "analyzeduration", "0", 0);

        const AVInputFormat* ifmt = av_find_input_format("x11grab");
        if (!ifmt) {
            LOG_ERROR() << "x11grab input format not found";
            av_dict_free(&opts);
            return false;
        }

        const char* display_env = std::getenv("DISPLAY");
        std::string display = display_env ? display_env : ":0";
        std::string url;

        if (target.type == ScreenCaptureTarget::Window && !target.id.empty()) {
            // Compositors redirect windows off-screen so window_id capture via XGetImage
            // fails with BadMatch. Instead, resolve the window's absolute screen position
            // and capture that region from the root — the compositor's output is always
            // readable there.
            int abs_x = 0, abs_y = 0;
            int cap_w = target.width, cap_h = target.height;
            bool got_abs = false;

            Display* dpy = XOpenDisplay(nullptr);
            if (dpy) {
                Window win { };
                try {
                    win = static_cast<Window>(std::stoul(target.id));
                } catch (...) {
                }

                if (win) {
                    Window child { };
                    XWindowAttributes attrs { };
                    if (XGetWindowAttributes(dpy, win, &attrs) && XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &abs_x, &abs_y, &child)) {
                        cap_w = attrs.width;
                        cap_h = attrs.height;
                        got_abs = true;
                    }
                }
                XCloseDisplay(dpy);
            }

            if (got_abs) {
                LOG_INFO() << "window capture: using root region at "
                           << abs_x << "," << abs_y << " " << cap_w << "x" << cap_h;
                url = display + "+" + std::to_string(abs_x) + "," + std::to_string(abs_y);
            } else {
                LOG_WARNING() << "window capture: could not resolve absolute position, "
                                 "falling back to window_id (may fail under compositor)";
                url = display;
                av_dict_set(&opts, "window_id", target.id.c_str(), 0);
            }
            std::string vsize = std::to_string(cap_w) + "x" + std::to_string(cap_h);
            av_dict_set(&opts, "video_size", vsize.c_str(), 0);
        } else {
            url = display + "+" + std::to_string(target.x) + "," + std::to_string(target.y);
            std::string vsize = std::to_string(target.width) + "x" + std::to_string(target.height);
            av_dict_set(&opts, "video_size", vsize.c_str(), 0);
        }

        av_dict_set(&opts, "draw_mouse", "1", 0);

        fmt_ctx_ = avformat_alloc_context();
        fmt_ctx_->interrupt_callback.callback = interrupt_cb;
        fmt_ctx_->interrupt_callback.opaque = this;

        LOG_INFO() << "opening capture: " << url;

        int ret = avformat_open_input(&fmt_ctx_, url.c_str(), ifmt, &opts);
        av_dict_free(&opts);
        if (ret < 0) {
            LOG_ERROR() << "avformat_open_input failed: " << ff_err(ret);
            fmt_ctx_ = nullptr;
            return false;
        }

        if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
            LOG_ERROR() << "avformat_find_stream_info failed";
            cleanup();
            return false;
        }

        video_idx_ = -1;
        for (unsigned i = 0; i < fmt_ctx_->nb_streams; ++i) {
            if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_idx_ = static_cast<int>(i);
                break;
            }
        }
        if (video_idx_ < 0) {
            LOG_ERROR() << "no video stream found";
            cleanup();
            return false;
        }

        auto* par = fmt_ctx_->streams[video_idx_]->codecpar;
        const AVCodec* dec = avcodec_find_decoder(par->codec_id);
        if (!dec) {
            LOG_ERROR() << "no decoder for capture format";
            cleanup();
            return false;
        }

        dec_ctx_ = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(dec_ctx_, par);
        if (avcodec_open2(dec_ctx_, dec, nullptr) < 0) {
            LOG_ERROR() << "failed to open capture decoder";
            cleanup();
            return false;
        }

        int src_w = dec_ctx_->width;
        int src_h = dec_ctx_->height;
        compute_output_size(src_w, src_h, max_w_, max_h_, out_w_, out_h_);

        sws_ = sws_getContext(src_w, src_h, dec_ctx_->pix_fmt, out_w_, out_h_,
            AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr,
            nullptr);
        if (!sws_) {
            LOG_ERROR() << "sws_getContext (capture) failed";
            cleanup();
            return false;
        }

        pkt_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        bgra_frame_ = av_frame_alloc();
        bgra_frame_->format = AV_PIX_FMT_BGRA;
        bgra_frame_->width = out_w_;
        bgra_frame_->height = out_h_;
        av_frame_get_buffer(bgra_frame_, 0);

        running_ = true;
        thread_ = std::thread(&LinuxScreenCapture::capture_loop, this);

        LOG_INFO() << "screen capture started: " << src_w << "x" << src_h << " -> "
                   << out_w_ << "x" << out_h_ << " @ " << fps << " fps";
        return true;
    }

    void stop() override
    {
        if (!running_.exchange(false)) {
            return;
        }
        stopping_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        stopping_ = false;
        cleanup();
        LOG_INFO() << "screen capture stopped";
    }

    bool running() const override { return running_; }

private:
    static int interrupt_cb(void* opaque)
    {
        auto* self = static_cast<LinuxScreenCapture*>(opaque);
        return self->stopping_.load() ? 1 : 0;
    }

    void capture_loop()
    {
        int consecutive_errors = 0;
        static constexpr int kMaxConsecutiveErrors = 20;

        while (running_) {
            int ret = av_read_frame(fmt_ctx_, pkt_);
            if (ret < 0) {
                if (ret == AVERROR_EOF || stopping_) {
                    break;
                }
                ++consecutive_errors;
                if (consecutive_errors == kMaxConsecutiveErrors) {
                    LOG_WARNING() << "screen capture: " << consecutive_errors
                                  << " consecutive read errors, stopping capture";
                    running_ = false;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            consecutive_errors = 0;

            if (pkt_->stream_index != video_idx_) {
                av_packet_unref(pkt_);
                continue;
            }

            avcodec_send_packet(dec_ctx_, pkt_);
            while (avcodec_receive_frame(dec_ctx_, frame_) >= 0) {
                av_frame_make_writable(bgra_frame_);
                sws_scale(sws_, frame_->data, frame_->linesize, 0, dec_ctx_->height,
                    bgra_frame_->data, bgra_frame_->linesize);

                Frame out;
                out.width = out_w_;
                out.height = out_h_;
                auto row_bytes = static_cast<size_t>(out_w_) * 4;
                auto nbytes = row_bytes * out_h_;
                out.data.resize(nbytes);

                const uint8_t* src = bgra_frame_->data[0];
                int stride = bgra_frame_->linesize[0];
                if (static_cast<size_t>(stride) == row_bytes) {
                    std::memcpy(out.data.data(), src, nbytes);
                } else {
                    for (int y = 0; y < out_h_; ++y) {
                        std::memcpy(out.data.data() + y * row_bytes, src + y * stride,
                            row_bytes);
                    }
                }

                if (callback_ && running_) {
                    callback_(std::move(out));
                }
            }

            av_packet_unref(pkt_);
        }
    }

    void cleanup()
    {
        if (bgra_frame_) {
            av_frame_free(&bgra_frame_);
        }
        if (frame_) {
            av_frame_free(&frame_);
        }
        if (pkt_) {
            av_packet_free(&pkt_);
        }
        if (sws_) {
            sws_freeContext(sws_);
            sws_ = nullptr;
        }
        if (dec_ctx_) {
            avcodec_free_context(&dec_ctx_);
        }
        if (fmt_ctx_) {
            avformat_close_input(&fmt_ctx_);
        }
        video_idx_ = -1;
        out_w_ = 0;
        out_h_ = 0;
    }

    std::atomic<bool> running_ { false };
    std::atomic<bool> stopping_ { false };
    FrameCallback callback_;
    std::thread thread_;
    int max_w_ = 1920;
    int max_h_ = 1080;

    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* dec_ctx_ = nullptr;
    SwsContext* sws_ = nullptr;
    AVPacket* pkt_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* bgra_frame_ = nullptr;
    int video_idx_ = -1;
    int out_w_ = 0;
    int out_h_ = 0;
};

std::unique_ptr<ScreenCapture> ScreenCapture::create()
{
    return std::make_unique<LinuxScreenCapture>();
}
