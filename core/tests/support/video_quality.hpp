#pragma once

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace test_util {

// PSNR and SSIM between the frame that was captured and the frame that was
// rendered, computed by FFmpeg's own `psnr` and `ssim` filters rather than by
// anything written here.
//
// Both filters take two inputs and pass the main one through with the score in
// frame metadata, so comparing on both means two graphs; at the resolutions
// these scenarios use, filtering each frame twice costs nothing.
class FrameComparator {
public:
    struct Result {
        double psnr_avg_db = 0.0;
        double psnr_p05_db = 0.0; // worst 5% of frames
        double ssim_avg = 0.0;
        double ssim_p05 = 0.0;
        size_t frames = 0;
    };

    // FFmpeg reports infinite PSNR for a bit-exact frame. Reported as a finite
    // ceiling instead, so an average stays a number and the report stays JSON.
    static constexpr double kMaxPsnrDb = 100.0;

    FrameComparator(int width, int height)
        : w_(width)
        , h_(height)
    {
        av_log_set_level(AV_LOG_ERROR); // the filters narrate every frame otherwise
        ok_ = build("psnr", psnr_) && build("ssim", ssim_) && alloc_frames();
    }

    ~FrameComparator()
    {
        av_frame_free(&ref_yuv_);
        av_frame_free(&dist_yuv_);
        av_frame_free(&out_);
        avfilter_graph_free(&psnr_.graph);
        avfilter_graph_free(&ssim_.graph);
        sws_freeContext(ref_sws_);
        sws_freeContext(dist_sws_);
    }

    FrameComparator(const FrameComparator&) = delete;
    FrameComparator& operator=(const FrameComparator&) = delete;

    bool ok() const noexcept { return ok_; }
    const std::string& error() const noexcept { return error_; }

    // ref is BGRA as captured, dist is RGBA as the renderer received it.
    void add_pair(const uint8_t* ref_bgra, const uint8_t* dist_rgba)
    {
        if (!ok_) {
            return;
        }
        to_yuv(ref_sws_, AV_PIX_FMT_BGRA, ref_bgra, ref_yuv_);
        to_yuv(dist_sws_, AV_PIX_FMT_RGBA, dist_rgba, dist_yuv_);
        ref_yuv_->pts = dist_yuv_->pts = pts_++;

        if (double v = 0.0; push(psnr_, "lavfi.psnr.psnr_avg", v)) {
            psnr_.values.push_back(std::min(v, kMaxPsnrDb));
        }
        if (double v = 0.0; push(ssim_, "lavfi.ssim.All", v)) {
            ssim_.values.push_back(v);
        }
    }

    Result finish()
    {
        Result r;
        r.frames = psnr_.values.size();
        r.psnr_avg_db = mean(psnr_.values);
        r.psnr_p05_db = percentile(psnr_.values, 5);
        r.ssim_avg = mean(ssim_.values);
        r.ssim_p05 = percentile(ssim_.values, 5);
        return r;
    }

private:
    struct Graph {
        AVFilterGraph* graph = nullptr;
        AVFilterContext* main_src = nullptr;
        AVFilterContext* ref_src = nullptr;
        AVFilterContext* sink = nullptr;
        std::vector<double> values;
    };

    bool fail(std::string what)
    {
        error_ = std::move(what);
        return false;
    }

    bool build(const char* filter_name, Graph& g)
    {
        g.graph = avfilter_graph_alloc();
        if (!g.graph) {
            return fail("avfilter_graph_alloc");
        }

        char args[256];
        std::snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=1/1000:pixel_aspect=1/1",
            w_, h_, static_cast<int>(AV_PIX_FMT_YUV420P));

        const AVFilter* buffer = avfilter_get_by_name("buffer");
        const AVFilter* sink = avfilter_get_by_name("buffersink");
        const AVFilter* metric = avfilter_get_by_name(filter_name);
        if (!buffer || !sink || !metric) {
            return fail(std::string("filter not available: ") + filter_name);
        }

        AVFilterContext* metric_ctx = nullptr;
        if (avfilter_graph_create_filter(&g.main_src, buffer, "main", args, nullptr, g.graph) < 0
            || avfilter_graph_create_filter(&g.ref_src, buffer, "ref", args, nullptr, g.graph) < 0
            || avfilter_graph_create_filter(&metric_ctx, metric, "metric", nullptr, nullptr, g.graph) < 0
            || avfilter_graph_create_filter(&g.sink, sink, "out", nullptr, nullptr, g.graph) < 0) {
            return fail("avfilter_graph_create_filter");
        }

        // psnr/ssim take the distorted stream first and the reference second.
        if (avfilter_link(g.main_src, 0, metric_ctx, 0) < 0
            || avfilter_link(g.ref_src, 0, metric_ctx, 1) < 0
            || avfilter_link(metric_ctx, 0, g.sink, 0) < 0) {
            return fail("avfilter_link");
        }
        if (avfilter_graph_config(g.graph, nullptr) < 0) {
            return fail("avfilter_graph_config");
        }
        return true;
    }

    bool alloc_frames()
    {
        ref_yuv_ = alloc_yuv();
        dist_yuv_ = alloc_yuv();
        out_ = av_frame_alloc();
        return ref_yuv_ && dist_yuv_ && out_;
    }

    AVFrame* alloc_yuv()
    {
        AVFrame* f = av_frame_alloc();
        if (!f) {
            return nullptr;
        }
        f->format = AV_PIX_FMT_YUV420P;
        f->width = w_;
        f->height = h_;
        if (av_frame_get_buffer(f, 0) < 0) {
            av_frame_free(&f);
            return nullptr;
        }
        return f;
    }

    void to_yuv(SwsContext*& ctx, AVPixelFormat src_fmt, const uint8_t* src, AVFrame* dst)
    {
        if (!ctx) {
            ctx = sws_getContext(w_, h_, src_fmt, w_, h_, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
        }
        const uint8_t* planes[4] = { src, nullptr, nullptr, nullptr };
        const int strides[4] = { w_ * 4, 0, 0, 0 };
        sws_scale(ctx, planes, strides, 0, h_, dst->data, dst->linesize);
    }

    // Feeds one pair and reads `key` off the frame the filter emits.
    bool push(Graph& g, const char* key, double& out_value)
    {
        if (av_buffersrc_add_frame_flags(g.main_src, dist_yuv_,
                AV_BUFFERSRC_FLAG_KEEP_REF)
                < 0
            || av_buffersrc_add_frame_flags(g.ref_src, ref_yuv_,
                   AV_BUFFERSRC_FLAG_KEEP_REF)
                < 0) {
            return false;
        }
        if (av_buffersink_get_frame(g.sink, out_) < 0) {
            return false;
        }
        bool found = false;
        if (AVDictionaryEntry* e = av_dict_get(out_->metadata, key, nullptr, 0)) {
            out_value = std::atof(e->value);
            found = true;
        }
        av_frame_unref(out_);
        return found;
    }

    static double mean(const std::vector<double>& v)
    {
        if (v.empty()) {
            return 0.0;
        }
        double s = 0.0;
        for (double x : v) {
            s += x;
        }
        return s / static_cast<double>(v.size());
    }

    // Low percentile: these metrics are "higher is better", so the interesting
    // tail is the bottom of the distribution.
    static double percentile(std::vector<double> v, int percent)
    {
        if (v.empty()) {
            return 0.0;
        }
        const size_t rank = std::min(v.size() - 1,
            static_cast<size_t>(v.size() * static_cast<size_t>(percent) / 100));
        auto nth = v.begin() + static_cast<ptrdiff_t>(rank);
        std::nth_element(v.begin(), nth, v.end());
        return *nth;
    }

    int w_;
    int h_;
    bool ok_ = false;
    std::string error_;
    int64_t pts_ = 0;

    Graph psnr_;
    Graph ssim_;
    AVFrame* ref_yuv_ = nullptr;
    AVFrame* dist_yuv_ = nullptr;
    AVFrame* out_ = nullptr;
    SwsContext* ref_sws_ = nullptr;
    SwsContext* dist_sws_ = nullptr;
};

} // namespace test_util
