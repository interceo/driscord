extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "screen_capture.hpp"
#include "screen_capture_common.hpp"
#include "video/video_codec.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "log.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

// --- X11 RAII helpers -------------------------------------------------------

namespace {

struct DisplayDeleter {
    void operator()(Display* d) const noexcept
    {
        if (d)
            XCloseDisplay(d);
    }
};
using DisplayPtr = std::unique_ptr<Display, DisplayDeleter>;

struct XRRScreenResourcesDeleter {
    void operator()(XRRScreenResources* r) const noexcept
    {
        if (r)
            XRRFreeScreenResources(r);
    }
};
using XRRScreenResourcesPtr = std::unique_ptr<XRRScreenResources, XRRScreenResourcesDeleter>;

struct XRRCrtcInfoDeleter {
    void operator()(XRRCrtcInfo* c) const noexcept
    {
        if (c)
            XRRFreeCrtcInfo(c);
    }
};
using XRRCrtcInfoPtr = std::unique_ptr<XRRCrtcInfo, XRRCrtcInfoDeleter>;

struct XRROutputInfoDeleter {
    void operator()(XRROutputInfo* o) const noexcept
    {
        if (o)
            XRRFreeOutputInfo(o);
    }
};
using XRROutputInfoPtr = std::unique_ptr<XRROutputInfo, XRROutputInfoDeleter>;

struct XImageDeleter {
    void operator()(XImage* img) const noexcept
    {
        if (img)
            XDestroyImage(img);
    }
};
using XImagePtr = std::unique_ptr<XImage, XImageDeleter>;

// XFree handles allocations returned by XGetWindowProperty, XFetchName, etc.
struct XFreeDeleter {
    void operator()(void* p) const noexcept
    {
        if (p)
            XFree(p);
    }
};
template <typename T>
using XFreePtr = std::unique_ptr<T, XFreeDeleter>;

} // namespace

// --- target enumeration -----------------------------------------------------

static std::string x11_window_name(Display* dpy, Window win)
{
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);

    Atom type { };
    int format { };
    unsigned long nitems { }, after { };
    unsigned char* raw = nullptr;

    if (XGetWindowProperty(dpy, win, net_wm_name, 0, 256, False, utf8, &type,
            &format, &nitems, &after, &raw)
            == Success
        && raw && nitems > 0) {
        XFreePtr<unsigned char> data { raw };
        return { reinterpret_cast<char*>(data.get()), nitems };
    }

    char* wm_name_raw = nullptr;
    if (XFetchName(dpy, win, &wm_name_raw) && wm_name_raw) {
        XFreePtr<char> wm_name { wm_name_raw };
        return { wm_name.get() };
    }
    return { };
}

std::vector<ScreenCaptureTarget> ScreenCapture::list_targets()
{
    std::vector<ScreenCaptureTarget> targets;

    DisplayPtr dpy { XOpenDisplay(nullptr) };
    if (!dpy) {
        return targets;
    }

    int screen = DefaultScreen(dpy.get());
    Window root = RootWindow(dpy.get(), screen);

    if (XRRScreenResourcesPtr res { XRRGetScreenResources(dpy.get(), root) }) {
        targets.reserve(res->ncrtc);
        for (int i = 0; i < res->ncrtc; ++i) {
            XRRCrtcInfoPtr crtc { XRRGetCrtcInfo(dpy.get(), res.get(), res->crtcs[i]) };
            if (!crtc) {
                continue;
            }
            if (crtc->mode == None || crtc->width == 0 || crtc->height == 0) {
                continue;
            }

            std::string name = "Monitor " + std::to_string(i + 1);
            if (crtc->noutput > 0) {
                XRROutputInfoPtr output { XRRGetOutputInfo(dpy.get(), res.get(), crtc->outputs[0]) };
                if (output) {
                    name = std::string(output->name, static_cast<size_t>(output->nameLen));
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
        }
    }

    if (targets.empty()) {
        XWindowAttributes root_attrs { };
        XGetWindowAttributes(dpy.get(), root, &root_attrs);
        ScreenCaptureTarget t;
        t.type = ScreenCaptureTarget::Monitor;
        t.id = "0";
        t.name = "Full Desktop (" + std::to_string(root_attrs.width) + "x" + std::to_string(root_attrs.height) + ")";
        t.width = root_attrs.width;
        t.height = root_attrs.height;
        targets.emplace_back(std::move(t));
    }

    Atom net_client_list = XInternAtom(dpy.get(), "_NET_CLIENT_LIST", False);
    Atom type { };
    int format { };
    unsigned long nitems { }, after { };
    unsigned char* raw = nullptr;

    if (XGetWindowProperty(dpy.get(), root, net_client_list, 0, ~0L, False, XA_WINDOW,
            &type, &format, &nitems, &after, &raw)
            == Success
        && raw) {
        XFreePtr<unsigned char> data { raw };
        auto* windows = reinterpret_cast<Window*>(data.get());
        for (unsigned long i = 0; i < nitems; ++i) {
            XWindowAttributes attrs { };
            if (!XGetWindowAttributes(dpy.get(), windows[i], &attrs)) {
                continue;
            }
            if (attrs.map_state != IsViewable) {
                continue;
            }
            if (attrs.width < 50 || attrs.height < 50) {
                continue;
            }

            std::string name = x11_window_name(dpy.get(), windows[i]);
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
    }

    return targets;
}

// --- thumbnail --------------------------------------------------------------

ScreenCapture::Frame ScreenCapture::grab_thumbnail(
    const ScreenCaptureTarget& target,
    int max_w,
    int max_h)
{
    Frame f;
    DisplayPtr dpy { XOpenDisplay(nullptr) };
    if (!dpy) {
        return f;
    }

    int scr = DefaultScreen(dpy.get());
    Window root = RootWindow(dpy.get(), scr);

    int src_x = 0, src_y = 0, src_w = 0, src_h = 0;
    Window grab_win = root;

    if (target.type == ScreenCaptureTarget::Window && !target.id.empty()) {
        try {
            grab_win = static_cast<Window>(std::stoul(target.id));
        } catch (const std::exception& e) {
            LOG_WARNING() << "grab_thumbnail: invalid window id '" << target.id << "': " << e.what();
            return f;
        }
        XWindowAttributes attrs { };
        if (!XGetWindowAttributes(dpy.get(), grab_win, &attrs)) {
            LOG_WARNING() << "grab_thumbnail: XGetWindowAttributes failed for window " << target.id;
            return f;
        }
        src_w = attrs.width;
        src_h = attrs.height;
    } else {
        src_x = target.x;
        src_y = target.y;
        src_w = target.width;
        src_h = target.height;
    }

    if (src_w <= 0 || src_h <= 0) {
        return f;
    }

    XImagePtr img { XGetImage(dpy.get(), grab_win, src_x, src_y, static_cast<unsigned>(src_w),
        static_cast<unsigned>(src_h), AllPlanes, ZPixmap) };
    if (!img) {
        return f;
    }

    std::vector<uint8_t> bgra(static_cast<size_t>(src_w) * src_h * 4);
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w; ++x) {
            unsigned long pixel = XGetPixel(img.get(), x, y);
            size_t idx = (static_cast<size_t>(y) * src_w + x) * 4;
            bgra[idx + 0] = static_cast<uint8_t>(pixel & 0xFF);
            bgra[idx + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);
            bgra[idx + 2] = static_cast<uint8_t>((pixel >> 16) & 0xFF);
            bgra[idx + 3] = 255;
        }
    }

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

            if (DisplayPtr dpy { XOpenDisplay(nullptr) }) {
                Window win { };
                try {
                    win = static_cast<Window>(std::stoul(target.id));
                } catch (const std::exception& e) {
                    LOG_WARNING() << "start: invalid window id '" << target.id << "': " << e.what();
                }

                if (win) {
                    Window child { };
                    XWindowAttributes attrs { };
                    if (XGetWindowAttributes(dpy.get(), win, &attrs) && XTranslateCoordinates(dpy.get(), win, DefaultRootWindow(dpy.get()), 0, 0, &abs_x, &abs_y, &child)) {
                        cap_w = attrs.width;
                        cap_h = attrs.height;
                        got_abs = true;
                    } else {
                        LOG_WARNING() << "start: XGetWindowAttributes or XTranslateCoordinates failed for window " << target.id;
                    }
                }
            } else {
                LOG_WARNING() << "start: XOpenDisplay failed, window position unknown";
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

        AVFormatContext* fmt_raw = avformat_alloc_context();
        if (!fmt_raw) {
            LOG_ERROR() << "avformat_alloc_context failed (OOM)";
            av_dict_free(&opts);
            return false;
        }
        fmt_raw->interrupt_callback.callback = interrupt_cb;
        fmt_raw->interrupt_callback.opaque = this;

        LOG_INFO() << "opening capture: " << url;

        int ret = avformat_open_input(&fmt_raw, url.c_str(), ifmt, &opts);
        av_dict_free(&opts);
        if (ret < 0) {
            LOG_ERROR() << "avformat_open_input failed: " << ff_err(ret);
            // avformat_open_input frees fmt_raw and sets it to nullptr on failure.
            return false;
        }
        fmt_ctx_.reset(fmt_raw);

        if (avformat_find_stream_info(fmt_ctx_.get(), nullptr) < 0) {
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

        dec_ctx_ = ff::CodecContextPtr { avcodec_alloc_context3(dec) };
        if (!dec_ctx_) {
            LOG_ERROR() << "avcodec_alloc_context3 failed (OOM)";
            cleanup();
            return false;
        }

        if (avcodec_parameters_to_context(dec_ctx_.get(), par) < 0) {
            LOG_ERROR() << "avcodec_parameters_to_context failed";
            cleanup();
            return false;
        }

        if (avcodec_open2(dec_ctx_.get(), dec, nullptr) < 0) {
            LOG_ERROR() << "failed to open capture decoder";
            cleanup();
            return false;
        }

        int src_w = dec_ctx_->width;
        int src_h = dec_ctx_->height;
        compute_output_size(src_w, src_h, max_w_, max_h_, out_w_, out_h_);

        sws_ = ff::SwsContextPtr { sws_getContext(src_w, src_h, dec_ctx_->pix_fmt, out_w_, out_h_,
            AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr) };
        if (!sws_) {
            LOG_ERROR() << "sws_getContext (capture) failed";
            cleanup();
            return false;
        }

        pkt_ = ff::PacketPtr { av_packet_alloc() };
        frame_ = ff::FramePtr { av_frame_alloc() };
        bgra_frame_ = ff::FramePtr { av_frame_alloc() };
        if (!pkt_ || !frame_ || !bgra_frame_) {
            LOG_ERROR() << "av_packet/frame_alloc failed (OOM)";
            cleanup();
            return false;
        }

        bgra_frame_->format = AV_PIX_FMT_BGRA;
        bgra_frame_->width = out_w_;
        bgra_frame_->height = out_h_;
        if (av_frame_get_buffer(bgra_frame_.get(), 0) < 0) {
            LOG_ERROR() << "av_frame_get_buffer failed (OOM)";
            cleanup();
            return false;
        }

        running_ = true;
        thread_ = std::thread(&LinuxScreenCapture::capture_loop, this);

        LOG_INFO() << "screen capture started: " << src_w << "x" << src_h << " -> "
                   << out_w_ << "x" << out_h_ << " @ " << fps << " fps";
        return true;
    }

    void stop() override
    {
        stopping_ = true;
        running_.store(false);
        if (thread_.joinable())
            thread_.join();
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
            int ret = av_read_frame(fmt_ctx_.get(), pkt_.get());
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
                av_packet_unref(pkt_.get());
                continue;
            }

            int sret = avcodec_send_packet(dec_ctx_.get(), pkt_.get());
            if (sret < 0 && sret != AVERROR(EAGAIN) && sret != AVERROR_EOF) {
                LOG_WARNING() << "avcodec_send_packet: " << ff_err(sret);
                av_packet_unref(pkt_.get());
                continue;
            }

            while (avcodec_receive_frame(dec_ctx_.get(), frame_.get()) >= 0) {
                if (av_frame_make_writable(bgra_frame_.get()) < 0) {
                    continue;
                }

                int stride = bgra_frame_->linesize[0];
                auto row_bytes = static_cast<size_t>(out_w_) * 4;
                if (stride <= 0 || static_cast<size_t>(stride) < row_bytes) {
                    LOG_WARNING() << "capture: unexpected bgra linesize " << stride << ", skipping frame";
                    continue;
                }

                sws_scale(sws_.get(), frame_->data, frame_->linesize, 0, dec_ctx_->height,
                    bgra_frame_->data, bgra_frame_->linesize);

                Frame out;
                out.width = out_w_;
                out.height = out_h_;
                auto nbytes = row_bytes * out_h_;
                out.data.resize(nbytes);

                const uint8_t* src = bgra_frame_->data[0];
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

            av_packet_unref(pkt_.get());
        }
    }

    void cleanup()
    {
        bgra_frame_.reset();
        frame_.reset();
        pkt_.reset();
        sws_.reset();
        dec_ctx_.reset();
        fmt_ctx_.reset();
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

    ff::FormatContextPtr fmt_ctx_;
    ff::CodecContextPtr dec_ctx_;
    ff::SwsContextPtr sws_;
    ff::PacketPtr pkt_;
    ff::FramePtr frame_;
    ff::FramePtr bgra_frame_;
    int video_idx_ = -1;
    int out_w_ = 0;
    int out_h_ = 0;
};

std::unique_ptr<ScreenCapture> ScreenCapture::create()
{
    return std::make_unique<LinuxScreenCapture>();
}
