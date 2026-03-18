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

// --- ff:: deleter definitions -----------------------------------------------

namespace ff {
void CodecContextDeleter::operator()(AVCodecContext* ctx) const noexcept {
    avcodec_free_context(&ctx);
}
void SwsContextDeleter::operator()(SwsContext* ctx) const noexcept {
    sws_freeContext(ctx);
}
void FrameDeleter::operator()(AVFrame* f) const noexcept {
    av_frame_free(&f);
}
void PacketDeleter::operator()(AVPacket* p) const noexcept {
    av_packet_free(&p);
}
} // namespace ff

namespace {

struct FFmpegLogInit {
    FFmpegLogInit() { av_log_set_level(AV_LOG_WARNING); }
};
static FFmpegLogInit ff_log_init;

static std::string ff_err(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(errnum, buf, sizeof(buf));
    return buf;
}

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

static void setup_rate_control(AVCodecContext* ctx, int bitrate_bps, std::string_view enc_name) {
    ctx->bit_rate       = bitrate_bps;
    ctx->rc_max_rate    = bitrate_bps;
    ctx->rc_buffer_size = static_cast<int>(bitrate_bps * 2);

    if (enc_name.find("videotoolbox") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "allow_sw", "true", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
        av_opt_set(ctx->priv_data, "constant_bit_rate", "true", 0);
    } else if (enc_name.find("libx264") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
        av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
        ctx->profile     = AV_PROFILE_H264_HIGH;
        ctx->rc_min_rate = bitrate_bps;
        av_opt_set(ctx->priv_data, "nal-hrd", "cbr", 0);
        av_opt_set(ctx->priv_data, "rc-lookahead", "0", 0);
        av_opt_set(ctx->priv_data, "repeat-headers", "1", 0);
        av_opt_set(ctx->priv_data, "vbv-maxrate", std::to_string(bitrate_bps / 1000).c_str(), 0);
        av_opt_set(ctx->priv_data, "vbv-bufsize", std::to_string(bitrate_bps / 500).c_str(), 0);
    } else if (enc_name.find("nvenc") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "preset", "p4", 0);
        av_opt_set(ctx->priv_data, "tune", "ll", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
    } else if (enc_name.find("vaapi") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "rc_mode", "CBR", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
    } else if (enc_name.find("h264_mf") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "rate_control", "cbr", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
    } else if (enc_name.find("h264_amf") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        av_opt_set(ctx->priv_data, "quality", "balanced", 0);
        ctx->profile = AV_PROFILE_H264_HIGH;
    } else if (enc_name.find("h264_qsv") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
        av_opt_set(ctx->priv_data, "scenario", "displayremoting", 0);
        ctx->rc_min_rate = bitrate_bps;
        ctx->profile     = AV_PROFILE_H264_HIGH;
    }
}

static int optimal_thread_count() {
    unsigned hw = std::thread::hardware_concurrency();
    return static_cast<int>(hw == 0 ? 2 : std::min(hw, 4u));
}

static int compute_bitrate(const int w, const int h, const int base_kbps) {
    constexpr double kRefPixels = 1920.0 * 1080.0;
    return static_cast<int>(base_kbps * (static_cast<double>(w) * h / kRefPixels));
}

static void setup_common_ctx(AVCodecContext* ctx, int width, int height, int fps) {
    ctx->width        = width;
    ctx->height       = height;
    ctx->time_base    = {1, fps};
    ctx->framerate    = {fps, 1};
    ctx->pix_fmt      = AV_PIX_FMT_YUV420P;
    ctx->color_range  = AVCOL_RANGE_MPEG;
    ctx->gop_size     = fps;
    ctx->max_b_frames = 0;
    ctx->thread_count = optimal_thread_count();
    ctx->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
}

static ff::CodecContextPtr try_open_encoder(const AVCodec* codec, int width, int height, int fps, int bitrate_kbps) {
    ff::CodecContextPtr ctx{avcodec_alloc_context3(codec)};
    if (!ctx) {
        LOG_ERROR() << "avcodec_alloc_context3 failed for " << codec->name;
        return nullptr;
    }
    setup_common_ctx(ctx.get(), width, height, fps);
    setup_rate_control(ctx.get(), bitrate_kbps, codec->name);

    if (const int ret = avcodec_open2(ctx.get(), codec, nullptr); ret < 0) {
        LOG_ERROR() << "avcodec_open2 (" << codec->name << ") failed: " << ff_err(ret);
        return nullptr;
    }
    return ctx;
}

} // namespace

// --- VideoEncoder -----------------------------------------------------------

bool VideoEncoder::init(const size_t width, const size_t height, const size_t fps, const size_t base_bitrate_kbps) {
    const int w            = static_cast<int>(width);
    const int h            = static_cast<int>(height);
    const int f            = static_cast<int>(fps);
    const int base_bitrate = static_cast<int>(base_bitrate_kbps);

    if (w == state_.width && h == state_.height && f == state_.fps && base_bitrate == state_.base_bitrate_kbps) {
        return true;
    }

    if ((w & 1) != 0 || (h & 1) != 0 || !w || !h) {
        LOG_ERROR() << "video encoder: dimensions must be even and non-zero (" << w << "x" << h << ")";
        return false;
    }

    if (f < 1 || f > 240) {
        LOG_ERROR() << "video encoder: fps must be between 1 and 240 (" << f << ")";
        return false;
    }

    if (base_bitrate < 1) {
        LOG_ERROR() << "video encoder: base bitrate must be positive (" << base_bitrate << ")";
        return false;
    }

    const auto bitrate = compute_bitrate(w, h, base_bitrate);

    const AVCodec* codec = pick_h264_encoder();
    if (!codec) {
        LOG_ERROR() << "H.264 encoder not found";
        return false;
    }

    LOG_INFO() << "selected video encoder: " << codec->name;

    shutdown();

    auto ctx = try_open_encoder(codec, w, h, f, bitrate);
    if (!ctx) {
        const bool is_hw = std::string_view(codec->name).find("libx264") == std::string_view::npos;
        if (!is_hw) {
            return false;
        }

        LOG_WARNING() << "falling back to libx264";
        codec = try_encoder("libx264");
        if (!codec) {
            codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            LOG_WARNING() << "libx264 not found, trying default H.264 encoder";
        }
        if (!codec) {
            LOG_ERROR() << "H.264 encoder not found";
            return false;
        }

        ctx = try_open_encoder(codec, w, h, f, bitrate);
        if (!ctx) {
            LOG_ERROR() << "failed to open " << codec->name << " encoder";
            return false;
        }
    }

    auto frame = ff::FramePtr{av_frame_alloc()};
    if (!frame) {
        LOG_ERROR() << "av_frame_alloc failed";
        return false;
    }
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width  = w;
    frame->height = h;
    av_frame_get_buffer(frame.get(), 0);

    auto pkt = ff::PacketPtr{av_packet_alloc()};
    if (!pkt) {
        LOG_ERROR() << "av_packet_alloc failed";
        return false;
    }

    auto sws = ff::SwsContextPtr{
        sws_getContext(w, h, AV_PIX_FMT_BGRA, w, h, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr)
    };
    if (!sws) {
        LOG_ERROR() << "sws_getContext (encoder) failed";
        return false;
    }

    ctx_   = std::move(ctx);
    frame_ = std::move(frame);
    pkt_   = std::move(pkt);
    sws_   = std::move(sws);

    state_ = State{
        .base_bitrate_kbps = base_bitrate,
        .fps               = f,
        .width             = w,
        .height            = h,
        .pts               = 0,
    };

    bytes_since_calc_ = 0;
    last_calc_        = utils::Now();
    measured_kbps_    = 0;

    LOG_INFO()
        << "video encoder: " << w << "x" << h << " @ " << f << " fps"
        << " H.264 (" << codec->name << ") bitrate=" << bitrate << " kbps"
        << " gop=" << ctx_->gop_size;
    return true;
}

void VideoEncoder::shutdown() {
    ctx_.reset();
    sws_.reset();
    frame_.reset();
    pkt_.reset();
    state_ = {};
}

const std::vector<uint8_t>& VideoEncoder::encode(const std::vector<uint8_t>& bgra, int width, int height) {
    encode_buf_.clear();

    if (!ctx_ || width != state_.width || height != state_.height) {
        return encode_buf_;
    }

    av_frame_make_writable(frame_.get());

    const uint8_t* src_slices[1] = {bgra.data()};
    int src_stride[1]            = {width * 4};
    sws_scale(sws_.get(), src_slices, src_stride, 0, height, frame_->data, frame_->linesize);

    frame_->pts       = static_cast<int64_t>(state_.pts++);
    frame_->pict_type = AV_PICTURE_TYPE_NONE;
    frame_->flags &= ~AV_FRAME_FLAG_KEY;
    if (force_keyframe_.exchange(false)) {
        frame_->pict_type = AV_PICTURE_TYPE_I;
        frame_->flags |= AV_FRAME_FLAG_KEY;
    }

    if (avcodec_send_frame(ctx_.get(), frame_.get()) < 0) {
        return encode_buf_;
    }

    while (avcodec_receive_packet(ctx_.get(), pkt_.get()) >= 0) {
        encode_buf_.insert(encode_buf_.end(), pkt_->data, pkt_->data + pkt_->size);
        av_packet_unref(pkt_.get());
    }

    if (!encode_buf_.empty()) {
        const auto now        = utils::Now();
        const auto elapsed_ms = utils::ElapsedMs(last_calc_, now);

        bytes_since_calc_ += encode_buf_.size();
        if (elapsed_ms >= 1000) {
            measured_kbps_    = static_cast<int>(bytes_since_calc_ * 8 / elapsed_ms);
            bytes_since_calc_ = 0;
            last_calc_        = now;
        }
    }

    return encode_buf_;
}

// --- VideoDecoder -----------------------------------------------------------

bool VideoDecoder::init() {
    shutdown();

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOG_ERROR() << "H.264 decoder not found";
        return false;
    }

    ff::CodecContextPtr ctx{avcodec_alloc_context3(codec)};
    if (!ctx) {
        LOG_ERROR() << "avcodec_alloc_context3 (decoder) failed";
        return false;
    }
    ctx->thread_count = optimal_thread_count();
    ctx->thread_type  = FF_THREAD_SLICE;

    if (const int ret = avcodec_open2(ctx.get(), codec, nullptr); ret < 0) {
        LOG_ERROR() << "avcodec_open2 (decoder) failed: " << ff_err(ret);
        return false;
    }

    ctx_   = std::move(ctx);
    frame_ = ff::FramePtr{av_frame_alloc()};
    pkt_   = ff::PacketPtr{av_packet_alloc()};

    LOG_INFO() << "video decoder: H.264 (" << codec->name << ")";
    return true;
}

void VideoDecoder::shutdown() {
    ctx_.reset();
    sws_.reset();
    frame_.reset();
    pkt_.reset();
    last_w_ = 0;
    last_h_ = 0;
}

bool VideoDecoder::decode(const uint8_t* data, size_t len, std::vector<uint8_t>& rgba_out, int& out_w, int& out_h) {
    if (!ctx_) {
        return false;
    }

    pkt_->data = const_cast<uint8_t*>(data);
    pkt_->size = static_cast<int>(len);

    int ret    = avcodec_send_packet(ctx_.get(), pkt_.get());
    pkt_->data = nullptr;
    pkt_->size = 0;
    if (ret < 0) {
        return false;
    }

    if (avcodec_receive_frame(ctx_.get(), frame_.get()) < 0) {
        return false;
    }

    const int w = frame_->width;
    const int h = frame_->height;

    if (w != last_w_ || h != last_h_) {
        sws_.reset(sws_getContext(
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
        ));
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
    int dst_stride[1]      = {w * 4};
    sws_scale(sws_.get(), frame_->data, frame_->linesize, 0, h, dst_slices, dst_stride);

    return true;
}
