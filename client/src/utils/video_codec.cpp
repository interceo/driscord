extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>
#include <string>
#include <thread>

#include "log.hpp"
#include "video_codec.hpp"

namespace {
struct FFmpegLogInit {
    FFmpegLogInit() { av_log_set_level(AV_LOG_WARNING); }
};
static FFmpegLogInit ff_log_init;
}  // namespace

static std::string ff_err(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(errnum, buf, sizeof(buf));
    return buf;
}

// --- encoder helper: try opening a named encoder ----------------------------

static const AVCodec* try_encoder(const char* name) {
    const AVCodec* c = avcodec_find_encoder_by_name(name);
    if (c) {
        LOG_INFO() << "found encoder: " << name;
    }
    return c;
}

static const AVCodec* pick_h264_encoder() {
#ifdef __APPLE__
    if (auto* c = try_encoder("h264_videotoolbox")) {
        return c;
    }
#elif defined(_WIN32)
    if (auto* c = try_encoder("h264_amf")) {
        return c;
    }
    if (auto* c = try_encoder("h264_nvenc")) {
        return c;
    }
    if (auto* c = try_encoder("h264_mf")) {
        return c;
    }
    if (auto* c = try_encoder("h264_qsv")) {
        return c;
    }
#elif defined(__linux__)
    if (auto* c = try_encoder("h264_nvenc")) {
        return c;
    }
    if (auto* c = try_encoder("h264_vaapi")) {
        return c;
    }
#endif
    if (auto* c = try_encoder("libx264")) {
        return c;
    }
    return avcodec_find_encoder(AV_CODEC_ID_H264);
}

static void setup_rate_control(AVCodecContext* ctx, int64_t bitrate_bps, const std::string& enc_name) {
    ctx->bit_rate = bitrate_bps;
    ctx->rc_max_rate = bitrate_bps;
    ctx->rc_buffer_size = static_cast<int>(bitrate_bps * 2);

    if (enc_name.find("videotoolbox") != std::string::npos) {
        av_opt_set(ctx->priv_data, "allow_sw", "true", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
        av_opt_set(ctx->priv_data, "constant_bit_rate", "true", 0);
    } else if (enc_name.find("libx264") != std::string::npos) {
        av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
        av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
        ctx->rc_min_rate = bitrate_bps;  // required for nal-hrd=cbr
        av_opt_set(ctx->priv_data, "nal-hrd", "cbr", 0);
        av_opt_set(ctx->priv_data, "rc-lookahead", "0", 0);
        av_opt_set(ctx->priv_data, "repeat-headers", "1", 0);
        av_opt_set(ctx->priv_data, "vbv-maxrate", std::to_string(bitrate_bps / 1000).c_str(), 0);
        av_opt_set(ctx->priv_data, "vbv-bufsize", std::to_string(bitrate_bps / 500).c_str(), 0);  // 2x bitrate in kbits
    } else if (enc_name.find("nvenc") != std::string::npos) {
        av_opt_set(ctx->priv_data, "preset", "p4", 0);
        av_opt_set(ctx->priv_data, "tune", "ll", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
    } else if (enc_name.find("vaapi") != std::string::npos) {
        av_opt_set(ctx->priv_data, "rc_mode", "CBR", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
    } else if (enc_name.find("h264_mf") != std::string::npos) {
        av_opt_set(ctx->priv_data, "rate_control", "cbr", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
    } else if (enc_name.find("h264_amf") != std::string::npos) {
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        av_opt_set(ctx->priv_data, "quality", "balanced", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
    } else if (enc_name.find("h264_qsv") != std::string::npos) {
        av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
        av_opt_set(ctx->priv_data, "scenario", "displayremoting", 0);
        ctx->rc_min_rate = bitrate_bps;
        ctx->profile = AV_PROFILE_H264_HIGH;
    }
}

static int optimal_thread_count() {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) {
        return 2;
    }
    return static_cast<int>(std::min(hw, 4u));
}

static void setup_common_ctx(AVCodecContext* ctx, int width, int height, int fps) {
    ctx->width = width;
    ctx->height = height;
    ctx->time_base = {1, fps};
    ctx->framerate = {fps, 1};
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->color_range = AVCOL_RANGE_MPEG;
    ctx->gop_size = fps;
    ctx->max_b_frames = 0;
    ctx->thread_count = optimal_thread_count();
    ctx->thread_type = FF_THREAD_SLICE;
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
}

// --- VideoEncoder -----------------------------------------------------------

VideoEncoder::~VideoEncoder() { shutdown(); }

int VideoEncoder::compute_bitrate(int w, int h, int base_kbps) {
    constexpr double kRefPixels = 1920.0 * 1080.0;
    double pixels = static_cast<double>(w) * h;
    int kbps = static_cast<int>(base_kbps * (pixels / kRefPixels));
    return std::max(kbps, 500);
}

bool VideoEncoder::init(int width, int height, int fps, int base_bitrate_kbps) {
    if (width == width_ && height == height_ && fps == fps_) {
        return true;
    }

    shutdown();

    if (width % 2 != 0 || height % 2 != 0) {
        LOG_ERROR() << "video encoder: dimensions must be even (" << width << "x" << height << ")";
        return false;
    }

    fps = std::clamp(fps, 1, 240);

    const AVCodec* codec = pick_h264_encoder();
    if (!codec) {
        LOG_ERROR() << "H.264 encoder not found";
        return false;
    }

    int bitrate_kbps = compute_bitrate(width, height, base_bitrate_kbps);
    int64_t bitrate_bps = static_cast<int64_t>(bitrate_kbps) * 1000;

    ctx_ = avcodec_alloc_context3(codec);
    setup_common_ctx(ctx_, width, height, fps);

    std::string enc_name(codec->name);
    bool is_hw = (enc_name.find("libx264") == std::string::npos);

    setup_rate_control(ctx_, bitrate_bps, enc_name);

    int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        LOG_ERROR() << "avcodec_open2 (encoder " << enc_name << ") failed: " << ff_err(ret);
        avcodec_free_context(&ctx_);

        if (is_hw) {
            LOG_WARNING() << "falling back to libx264";
            codec = try_encoder("libx264");
            if (!codec) {
                codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            }
            if (!codec) {
                return false;
            }

            ctx_ = avcodec_alloc_context3(codec);
            setup_common_ctx(ctx_, width, height, fps);

            enc_name = codec->name;
            setup_rate_control(ctx_, bitrate_bps, enc_name);

            ret = avcodec_open2(ctx_, codec, nullptr);
            if (ret < 0) {
                LOG_ERROR() << "avcodec_open2 (libx264 fallback) failed: " << ff_err(ret);
                avcodec_free_context(&ctx_);
                return false;
            }
        } else {
            return false;
        }
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
        SWS_FAST_BILINEAR,
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
    fps_ = fps;
    pts_ = 0;
    bytes_since_calc_ = 0;
    measured_kbps_ = 0;
    last_calc_ = utils::Now();

    LOG_INFO()
        << "video encoder: " << width << "x" << height << " @ " << fps << " fps"
        << " H.264 (" << enc_name << ") bitrate=" << bitrate_kbps << " kbps"
        << " time_base=1/" << fps;
    return true;
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

const std::vector<uint8_t>& VideoEncoder::encode(const uint8_t* bgra, int width, int height) {
    encode_buf_.clear();

    if (!ctx_ || width != width_ || height != height_) {
        return encode_buf_;
    }

    av_frame_make_writable(frame_);

    const uint8_t* src_slices[1] = {bgra};
    int src_stride[1] = {width * 4};
    sws_scale(sws_, src_slices, src_stride, 0, height, frame_->data, frame_->linesize);

    frame_->pts = pts_++;
    frame_->pict_type = AV_PICTURE_TYPE_NONE;
    frame_->flags &= ~AV_FRAME_FLAG_KEY;
    if (force_keyframe_.exchange(false)) {
        frame_->pict_type = AV_PICTURE_TYPE_I;
        frame_->flags |= AV_FRAME_FLAG_KEY;
    }

    int ret = avcodec_send_frame(ctx_, frame_);
    if (ret < 0) {
        return encode_buf_;
    }

    while (avcodec_receive_packet(ctx_, pkt_) >= 0) {
        encode_buf_.insert(encode_buf_.end(), pkt_->data, pkt_->data + pkt_->size);
        av_packet_unref(pkt_);
    }

    if (!encode_buf_.empty()) {
        bytes_since_calc_ += encode_buf_.size();
        auto now = utils::Now();
        auto elapsed_ms = utils::ElapsedMs(last_calc_, now);
        if (elapsed_ms >= 1000) {
            measured_kbps_ = static_cast<int>(bytes_since_calc_ * 8 / elapsed_ms);
            bytes_since_calc_ = 0;
            last_calc_ = now;
        }
    }

    return encode_buf_;
}

// --- VideoDecoder -----------------------------------------------------------

VideoDecoder::~VideoDecoder() { shutdown(); }

bool VideoDecoder::init() {
    shutdown();

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOG_ERROR() << "H.264 decoder not found";
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    ctx_->thread_count = optimal_thread_count();
    ctx_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        LOG_ERROR() << "avcodec_open2 (decoder) failed: " << ff_err(ret);
        avcodec_free_context(&ctx_);
        return false;
    }

    frame_ = av_frame_alloc();
    pkt_ = av_packet_alloc();
    LOG_INFO() << "video decoder: H.264 (" << codec->name << ")";
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
    pkt_->data = nullptr;
    pkt_->size = 0;
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
            SWS_FAST_BILINEAR,
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
    size_t needed = static_cast<size_t>(w) * h * 4;
    rgba_out.resize(needed);

    uint8_t* dst_slices[1] = {rgba_out.data()};
    int dst_stride[1] = {w * 4};
    sws_scale(sws_, frame_->data, frame_->linesize, 0, h, dst_slices, dst_stride);

    return true;
}
