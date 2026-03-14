extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "screen_capture.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "log.hpp"

#ifdef __linux__
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

// --- target enumeration (platform-specific) ---------------------------------

#ifdef __linux__

static std::string x11_window_name(Display* dpy, Window win) {
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);

    Atom type{};
    int format{};
    unsigned long nitems{}, after{};
    unsigned char* data = nullptr;

    if (XGetWindowProperty(dpy, win, net_wm_name, 0, 256, False, utf8,
                           &type, &format, &nitems, &after, &data) == Success &&
        data && nitems > 0) {
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
    return {};
}

std::vector<CaptureTarget> ScreenCapture::list_targets() {
    std::vector<CaptureTarget> targets;

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return targets;

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    XWindowAttributes root_attrs{};
    XGetWindowAttributes(dpy, root, &root_attrs);
    targets.push_back({
        std::to_string(static_cast<unsigned long>(root)),
        "Full Desktop (" + std::to_string(root_attrs.width) + "x" +
            std::to_string(root_attrs.height) + ")",
        root_attrs.width,
        root_attrs.height,
    });

    Atom net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    Atom type{};
    int format{};
    unsigned long nitems{}, after{};
    unsigned char* data = nullptr;

    if (XGetWindowProperty(dpy, root, net_client_list, 0, ~0L, False,
                           XA_WINDOW, &type, &format, &nitems, &after, &data) == Success &&
        data) {
        auto* windows = reinterpret_cast<Window*>(data);
        for (unsigned long i = 0; i < nitems; ++i) {
            XWindowAttributes attrs{};
            if (!XGetWindowAttributes(dpy, windows[i], &attrs)) continue;
            if (attrs.map_state != IsViewable) continue;
            if (attrs.width < 50 || attrs.height < 50) continue;

            std::string name = x11_window_name(dpy, windows[i]);
            if (name.empty()) continue;

            name += " (" + std::to_string(attrs.width) + "x" +
                    std::to_string(attrs.height) + ")";

            targets.push_back({
                std::to_string(static_cast<unsigned long>(windows[i])),
                std::move(name),
                attrs.width,
                attrs.height,
            });
        }
        XFree(data);
    }

    XCloseDisplay(dpy);
    return targets;
}

#elif defined(__APPLE__)

std::vector<CaptureTarget> ScreenCapture::list_targets() {
    std::vector<CaptureTarget> targets;

    CGDirectDisplayID displays[16];
    uint32_t count = 0;
    CGGetActiveDisplayList(16, displays, &count);

    for (uint32_t i = 0; i < count; ++i) {
        auto bounds = CGDisplayBounds(displays[i]);
        int w = static_cast<int>(bounds.size.width);
        int h = static_cast<int>(bounds.size.height);

        targets.push_back({
            std::to_string(i),
            "Display " + std::to_string(i + 1) +
                " (" + std::to_string(w) + "x" + std::to_string(h) + ")",
            w, h,
        });
    }
    return targets;
}

#endif

// --- FFmpeg capture implementation ------------------------------------------

static std::string ff_err(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(errnum, buf, sizeof(buf));
    return buf;
}

class FFmpegScreenCapture : public ScreenCapture {
public:
    ~FFmpegScreenCapture() override { stop(); }

    bool start(int fps, const CaptureTarget& target,
               int max_w, int max_h, FrameCallback cb) override {
        if (running_) return false;

        avdevice_register_all();

        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "framerate", std::to_string(fps).c_str(), 0);

#ifdef __linux__
        const AVInputFormat* ifmt = av_find_input_format("x11grab");
        const char* display_env = std::getenv("DISPLAY");
        std::string url = display_env ? display_env : ":0";

        av_dict_set(&opts, "window_id", target.id.c_str(), 0);
        std::string vsize = std::to_string(target.width) + "x" +
                            std::to_string(target.height);
        av_dict_set(&opts, "video_size", vsize.c_str(), 0);
        av_dict_set(&opts, "draw_mouse", "1", 0);
#elif defined(__APPLE__)
        const AVInputFormat* ifmt = av_find_input_format("avfoundation");
        std::string url = "Capture screen " + target.id + ":none";
        av_dict_set(&opts, "capture_cursor", "1", 0);
#else
        LOG_ERROR() << "unsupported platform for screen capture";
        av_dict_free(&opts);
        return false;
#endif

        fmt_ctx_ = avformat_alloc_context();
        int ret = avformat_open_input(&fmt_ctx_, url.c_str(), ifmt, &opts);
        av_dict_free(&opts);
        if (ret < 0) {
            LOG_ERROR() << "avformat_open_input failed: " << ff_err(ret);
            fmt_ctx_ = nullptr;
            return false;
        }

        if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
            LOG_ERROR() << "avformat_find_stream_info failed";
            cleanup_ffmpeg();
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
            cleanup_ffmpeg();
            return false;
        }

        auto* par = fmt_ctx_->streams[video_idx_]->codecpar;
        const AVCodec* dec = avcodec_find_decoder(par->codec_id);
        dec_ctx_ = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(dec_ctx_, par);
        if (avcodec_open2(dec_ctx_, dec, nullptr) < 0) {
            LOG_ERROR() << "failed to open capture decoder";
            cleanup_ffmpeg();
            return false;
        }

        int src_w = dec_ctx_->width;
        int src_h = dec_ctx_->height;

        if (src_w > max_w || src_h > max_h) {
            float scale = std::min(static_cast<float>(max_w) / src_w,
                                   static_cast<float>(max_h) / src_h);
            out_w_ = static_cast<int>(src_w * scale) & ~1;
            out_h_ = static_cast<int>(src_h * scale) & ~1;
        } else {
            out_w_ = src_w & ~1;
            out_h_ = src_h & ~1;
        }

        sws_ = sws_getContext(src_w, src_h, dec_ctx_->pix_fmt,
                              out_w_, out_h_, AV_PIX_FMT_BGRA,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) {
            LOG_ERROR() << "sws_getContext (capture) failed";
            cleanup_ffmpeg();
            return false;
        }

        pkt_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        bgra_frame_ = av_frame_alloc();
        bgra_frame_->format = AV_PIX_FMT_BGRA;
        bgra_frame_->width = out_w_;
        bgra_frame_->height = out_h_;
        av_frame_get_buffer(bgra_frame_, 0);

        callback_ = std::move(cb);
        running_ = true;
        thread_ = std::thread(&FFmpegScreenCapture::capture_loop, this);

        LOG_INFO() << "screen capture started: " << src_w << "x" << src_h
                   << " -> " << out_w_ << "x" << out_h_ << " @ " << fps << " fps";
        return true;
    }

    void stop() override {
        if (!running_) return;
        running_ = false;
        if (thread_.joinable()) thread_.join();
        cleanup_ffmpeg();
        LOG_INFO() << "screen capture stopped";
    }

    bool running() const override { return running_; }

private:
    void capture_loop() {
        while (running_) {
            int ret = av_read_frame(fmt_ctx_, pkt_);
            if (ret < 0) {
                if (ret == AVERROR_EOF) break;
                continue;
            }

            if (pkt_->stream_index != video_idx_) {
                av_packet_unref(pkt_);
                continue;
            }

            avcodec_send_packet(dec_ctx_, pkt_);
            while (avcodec_receive_frame(dec_ctx_, frame_) >= 0) {
                av_frame_make_writable(bgra_frame_);
                sws_scale(sws_, frame_->data, frame_->linesize, 0,
                          dec_ctx_->height,
                          bgra_frame_->data, bgra_frame_->linesize);

                Frame out;
                out.width = out_w_;
                out.height = out_h_;
                auto nbytes = static_cast<size_t>(out_w_) * out_h_ * 4;
                out.data.resize(nbytes);

                const uint8_t* src = bgra_frame_->data[0];
                int stride = bgra_frame_->linesize[0];
                for (int y = 0; y < out_h_; ++y) {
                    std::memcpy(out.data.data() + y * out_w_ * 4,
                                src + y * stride,
                                static_cast<size_t>(out_w_) * 4);
                }

                if (callback_) callback_(out);
            }

            av_packet_unref(pkt_);
        }
    }

    void cleanup_ffmpeg() {
        if (bgra_frame_) { av_frame_free(&bgra_frame_); }
        if (frame_) { av_frame_free(&frame_); }
        if (pkt_) { av_packet_free(&pkt_); }
        if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
        if (dec_ctx_) { avcodec_free_context(&dec_ctx_); }
        if (fmt_ctx_) { avformat_close_input(&fmt_ctx_); }
        video_idx_ = -1;
        out_w_ = 0;
        out_h_ = 0;
    }

    std::atomic<bool> running_{false};
    FrameCallback callback_;
    std::thread thread_;

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

std::unique_ptr<ScreenCapture> ScreenCapture::create() {
    return std::make_unique<FFmpegScreenCapture>();
}
