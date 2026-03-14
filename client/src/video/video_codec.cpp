extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <string>

#include "log.hpp"
#include "video_codec.hpp"

static std::string ff_err(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(errnum, buf, sizeof(buf));
    return buf;
}

// --- VideoEncoder -----------------------------------------------------------

VideoEncoder::~VideoEncoder() { shutdown(); }

bool VideoEncoder::init(int width, int height, int bitrate_kbps) {
    shutdown();

    if (width % 2 != 0 || height % 2 != 0) {
        LOG_ERROR() << "video encoder: dimensions must be even (" << width << "x" << height << ")";
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_VP8);
    if (!codec) {
        LOG_ERROR() << "VP8 encoder not found";
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    ctx_->width = width;
    ctx_->height = height;
    ctx_->time_base = {1, 1000};
    ctx_->bit_rate = static_cast<int64_t>(bitrate_kbps) * 1000;
    ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx_->gop_size = 150;
    ctx_->thread_count = 2;
    ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;

    av_opt_set(ctx_->priv_data, "deadline", "realtime", 0);
    av_opt_set(ctx_->priv_data, "cpu-used", "8", 0);
    av_opt_set(ctx_->priv_data, "error-resilient", "1", 0);
    av_opt_set_int(ctx_->priv_data, "lag-in-frames", 0, 0);

    int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        LOG_ERROR() << "avcodec_open2 (encoder) failed: " << ff_err(ret);
        avcodec_free_context(&ctx_);
        return false;
    }

    frame_ = av_frame_alloc();
    frame_->format = AV_PIX_FMT_YUV420P;
    frame_->width = width;
    frame_->height = height;
    av_frame_get_buffer(frame_, 0);

    pkt_ = av_packet_alloc();

    sws_ = sws_getContext(
        width,
        height,
        AV_PIX_FMT_BGRA,
        width,
        height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );
    if (!sws_) {
        LOG_ERROR() << "sws_getContext (encoder) failed";
        shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    pts_ = 0;

    LOG_INFO() << "video encoder: " << width << "x" << height << " VP8 @ " << bitrate_kbps << " kbps";
    return true;
}

bool VideoEncoder::reinit(int width, int height, int bitrate_kbps) {
    if (width == width_ && height == height_) {
        return true;
    }
    shutdown();
    return init(width, height, bitrate_kbps);
}

void VideoEncoder::shutdown() {
    if (sws_) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (pkt_) {
        av_packet_free(&pkt_);
    }
    if (frame_) {
        av_frame_free(&frame_);
    }
    if (ctx_) {
        avcodec_free_context(&ctx_);
    }
    width_ = 0;
    height_ = 0;
    pts_ = 0;
}

std::vector<uint8_t> VideoEncoder::encode(const uint8_t* bgra, int width, int height) {
    if (!ctx_ || width != width_ || height != height_) {
        return {};
    }

    av_frame_make_writable(frame_);

    const uint8_t* src_slices[1] = {bgra};
    int src_stride[1] = {width * 4};
    sws_scale(sws_, src_slices, src_stride, 0, height, frame_->data, frame_->linesize);

    frame_->pts = pts_++;

    int ret = avcodec_send_frame(ctx_, frame_);
    if (ret < 0) {
        return {};
    }

    std::vector<uint8_t> result;
    while (avcodec_receive_packet(ctx_, pkt_) >= 0) {
        result.insert(result.end(), pkt_->data, pkt_->data + pkt_->size);
        av_packet_unref(pkt_);
    }
    return result;
}

// --- VideoDecoder -----------------------------------------------------------

VideoDecoder::~VideoDecoder() { shutdown(); }

bool VideoDecoder::init() {
    shutdown();

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_VP8);
    if (!codec) {
        LOG_ERROR() << "VP8 decoder not found";
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    ctx_->thread_count = 2;

    int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        LOG_ERROR() << "avcodec_open2 (decoder) failed: " << ff_err(ret);
        avcodec_free_context(&ctx_);
        return false;
    }

    frame_ = av_frame_alloc();
    pkt_ = av_packet_alloc();
    return true;
}

void VideoDecoder::shutdown() {
    if (sws_) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (pkt_) {
        av_packet_free(&pkt_);
    }
    if (frame_) {
        av_frame_free(&frame_);
    }
    if (ctx_) {
        avcodec_free_context(&ctx_);
    }
    last_w_ = 0;
    last_h_ = 0;
}

bool VideoDecoder::decode(const uint8_t* data, size_t len, std::vector<uint8_t>& rgba_out, int& out_w, int& out_h) {
    if (!ctx_) {
        return false;
    }

    pkt_->data = const_cast<uint8_t*>(data);
    pkt_->size = static_cast<int>(len);

    int ret = avcodec_send_packet(ctx_, pkt_);
    if (ret < 0) {
        return false;
    }

    ret = avcodec_receive_frame(ctx_, frame_);
    if (ret < 0) {
        return false;
    }

    int w = frame_->width;
    int h = frame_->height;

    if (w != last_w_ || h != last_h_) {
        if (sws_) {
            sws_freeContext(sws_);
        }
        sws_ = sws_getContext(
            w,
            h,
            static_cast<AVPixelFormat>(frame_->format),
            w,
            h,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr
        );
        last_w_ = w;
        last_h_ = h;
    }

    if (!sws_) {
        return false;
    }

    out_w = w;
    out_h = h;
    rgba_out.resize(static_cast<size_t>(w) * h * 4);

    uint8_t* dst_slices[1] = {rgba_out.data()};
    int dst_stride[1] = {w * 4};
    sws_scale(sws_, frame_->data, frame_->linesize, 0, h, dst_slices, dst_stride);

    return true;
}
