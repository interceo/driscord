extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>
#include <string>
#include <thread>

#include "enum_strings.hpp"
#include "log.hpp"
#include "video_codec.hpp"

// --- ff:: deleter definitions -----------------------------------------------

namespace ff {
void CodecContextDeleter::operator()(AVCodecContext* ctx) const noexcept
{
    avcodec_free_context(&ctx);
}
void SwsContextDeleter::operator()(SwsContext* ctx) const noexcept
{
    sws_freeContext(ctx);
}
void FrameDeleter::operator()(AVFrame* f) const noexcept
{
    av_frame_free(&f);
}
void PacketDeleter::operator()(AVPacket* p) const noexcept
{
    av_packet_free(&p);
}
} // namespace ff

namespace {

struct FFmpegLogInit {
    FFmpegLogInit() { av_log_set_level(AV_LOG_WARNING); }
};
static FFmpegLogInit ff_log_init;

static std::string ff_err(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] { };
    av_strerror(errnum, buf, sizeof(buf));
    return buf;
}

static const AVCodec* try_encoder(const char* name)
{
    const AVCodec* c = avcodec_find_encoder_by_name(name);
    if (c) {
        LOG_INFO() << "found encoder: " << name;
    }
    return c;
}

static const AVCodec* try_decoder(const char* name)
{
    const AVCodec* c = avcodec_find_decoder_by_name(name);
    if (c) {
        LOG_INFO() << "found decoder: " << name;
    }
    return c;
}

struct HWDecoderSpec {
    const char* codec_name;
    AVHWDeviceType hw_type; // AV_HWDEVICE_TYPE_NONE → no external device ctx needed
};

// Priority order mirrors the encoder: NVENC/CUVID first, then vendor-specific.
// CUVID and VideoToolbox manage their own HW context internally; VAAPI/QSV need
// an explicit hw_device_ctx created here.
static constexpr HWDecoderSpec kHWDecodersH264[] = {
#ifdef __APPLE__
    // VideoToolbox manages its own HW context internally.
    { "h264_videotoolbox", AV_HWDEVICE_TYPE_NONE },
#elif defined(_WIN32)
    // Explicit CUDA context so CUVID works regardless of NVENC init order.
    { "h264_cuvid", AV_HWDEVICE_TYPE_CUDA },
    { "h264_qsv", AV_HWDEVICE_TYPE_QSV },
#elif defined(__linux__)
    { "h264_cuvid", AV_HWDEVICE_TYPE_CUDA },
    { "h264_vaapi", AV_HWDEVICE_TYPE_VAAPI },
    { "h264_qsv", AV_HWDEVICE_TYPE_QSV },
#endif
};

static constexpr HWDecoderSpec kHWDecodersHEVC[] = {
#ifdef __APPLE__
    { "hevc_videotoolbox", AV_HWDEVICE_TYPE_NONE },
#elif defined(_WIN32)
    { "hevc_cuvid", AV_HWDEVICE_TYPE_CUDA },
    { "hevc_qsv", AV_HWDEVICE_TYPE_QSV },
#elif defined(__linux__)
    { "hevc_cuvid", AV_HWDEVICE_TYPE_CUDA },
    { "hevc_vaapi", AV_HWDEVICE_TYPE_VAAPI },
    { "hevc_qsv", AV_HWDEVICE_TYPE_QSV },
#endif
};

static const AVCodec* pick_h264_encoder()
{
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

static const AVCodec* pick_hevc_encoder()
{
#ifdef __APPLE__
    if (auto* c = try_encoder("hevc_videotoolbox")) {
        return c;
    }
#elif defined(_WIN32)
    if (auto* c = try_encoder("hevc_amf")) {
        return c;
    }
    if (auto* c = try_encoder("hevc_nvenc")) {
        return c;
    }
    if (auto* c = try_encoder("hevc_mf")) {
        return c;
    }
    if (auto* c = try_encoder("hevc_qsv")) {
        return c;
    }
#elif defined(__linux__)
    if (auto* c = try_encoder("hevc_nvenc")) {
        return c;
    }
    if (auto* c = try_encoder("hevc_vaapi")) {
        return c;
    }
#endif
    if (auto* c = try_encoder("libx265")) {
        return c;
    }
    return avcodec_find_encoder(AV_CODEC_ID_HEVC);
}

static void setup_rate_control(AVCodecContext* ctx,
    int bitrate_kbps,
    std::string_view enc_name)
{
    const bool is_hevc = enc_name.find("hevc") != std::string_view::npos
        || enc_name.find("x265") != std::string_view::npos;
    const int profile = is_hevc ? AV_PROFILE_HEVC_MAIN : AV_PROFILE_H264_HIGH;

    const int64_t bitrate_bps = static_cast<int64_t>(bitrate_kbps) * 1000;
    ctx->bit_rate = bitrate_bps;
    ctx->rc_max_rate = bitrate_bps;
    ctx->rc_buffer_size = static_cast<int>(bitrate_bps * 2);

    if (enc_name.find("videotoolbox") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "allow_sw", "true", 0);
        ctx->profile = profile;
        av_opt_set(ctx->priv_data, "constant_bit_rate", "true", 0);
    } else if (enc_name.find("libx264") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
        av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
        ctx->profile = profile;
        ctx->rc_min_rate = bitrate_bps;
        av_opt_set(ctx->priv_data, "nal-hrd", "cbr", 0);
        av_opt_set(ctx->priv_data, "rc-lookahead", "0", 0);
        av_opt_set(ctx->priv_data, "repeat-headers", "1", 0);
        // vbv-maxrate and vbv-bufsize are in kbps for libx264
        av_opt_set(ctx->priv_data, "vbv-maxrate",
            std::to_string(bitrate_kbps).c_str(), 0);
        av_opt_set(ctx->priv_data, "vbv-bufsize",
            std::to_string(bitrate_kbps * 2).c_str(), 0);
    } else if (enc_name.find("libx265") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
        ctx->profile = profile;
        ctx->rc_min_rate = bitrate_bps;
        // x265 VBV params and repeat-headers go through x265-params
        av_opt_set(ctx->priv_data, "x265-params",
            ("vbv-maxrate=" + std::to_string(bitrate_kbps)
                + ":vbv-bufsize=" + std::to_string(bitrate_kbps * 2)
                + ":repeat-headers=1:no-info=1")
                .c_str(),
            0);
    } else if (enc_name.find("nvenc") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "preset", "p4", 0);
        av_opt_set(ctx->priv_data, "tune", "ll", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        av_opt_set(ctx->priv_data, "repeat_headers", "1", 0); // SPS/PPS in every IDR
        ctx->profile = profile;
    } else if (enc_name.find("vaapi") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "rc_mode", "CBR", 0);
        av_opt_set(ctx->priv_data, "repeat_headers", "1", 0); // SPS/PPS in every IDR
        ctx->profile = profile;
    } else if (enc_name.find("_mf") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "rate_control", "cbr", 0);
        ctx->profile = profile;
    } else if (enc_name.find("_amf") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        av_opt_set(ctx->priv_data, "quality", "balanced", 0);
        av_opt_set(ctx->priv_data, "repeat_headers", "1", 0); // SPS/PPS in every IDR
        ctx->profile = profile;
    } else if (enc_name.find("_qsv") != std::string_view::npos) {
        av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
        av_opt_set(ctx->priv_data, "scenario", "displayremoting", 0);
        av_opt_set(ctx->priv_data, "repeat_headers", "1", 0); // SPS/PPS in every IDR
        ctx->rc_min_rate = bitrate_bps;
        ctx->profile = profile;
    }
}

static int optimal_thread_count()
{
    unsigned hw = std::thread::hardware_concurrency();
    return static_cast<int>(hw == 0 ? 2 : std::min(hw, 4u));
}

static int compute_bitrate(const int w, const int h, const int base_kbps)
{
    constexpr double kRefPixels = 1920.0 * 1080.0;
    return static_cast<int>(base_kbps * (static_cast<double>(w) * h / kRefPixels));
}

static void setup_common_ctx(AVCodecContext* ctx,
    int width,
    int height,
    int fps)
{
    ctx->width = width;
    ctx->height = height;
    ctx->time_base = { 1, fps };
    ctx->framerate = { fps, 1 };
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->color_range = AVCOL_RANGE_MPEG;
    ctx->gop_size = fps;
    ctx->max_b_frames = 0;
    ctx->thread_count = optimal_thread_count();
    ctx->thread_type = FF_THREAD_SLICE; // FF_THREAD_FRAME adds latency = thread_count-1 frames,
                                        // incompatible with LOW_DELAY
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
}

static ff::CodecContextPtr try_open_encoder(const AVCodec* codec,
    int width,
    int height,
    int fps,
    int bitrate_kbps)
{
    ff::CodecContextPtr ctx { avcodec_alloc_context3(codec) };
    if (!ctx) {
        LOG_ERROR() << "avcodec_alloc_context3 failed for " << codec->name;
        return nullptr;
    }
    setup_common_ctx(ctx.get(), width, height, fps);
    setup_rate_control(ctx.get(), bitrate_kbps, codec->name);

    if (const int ret = avcodec_open2(ctx.get(), codec, nullptr); ret < 0) {
        LOG_ERROR() << "avcodec_open2 (" << codec->name
                    << ") failed: " << ff_err(ret);
        return nullptr;
    }
    return ctx;
}

} // namespace

// --- VideoEncoder -----------------------------------------------------------

utils::Expected<void, VideoError> VideoEncoder::init(const size_t width,
    const size_t height,
    const size_t fps,
    const size_t base_bitrate_kbps)
{
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    const int f = static_cast<int>(fps);
    const int base_bitrate = static_cast<int>(base_bitrate_kbps);

    if (w == state_.width && h == state_.height && f == state_.fps && base_bitrate == state_.base_bitrate_kbps) {
        return { };
    }

    if ((w & 1) != 0 || (h & 1) != 0 || !w || !h) {
        LOG_ERROR() << "video encoder: dimensions must be even and non-zero (" << w
                    << "x" << h << ")";
        return utils::Unexpected(VideoError::InvalidDimensions);
    }

    if (f < 1 || f > 240) {
        LOG_ERROR() << "video encoder: fps must be between 1 and 240 (" << f << ")";
        return utils::Unexpected(VideoError::InvalidFps);
    }

    if (base_bitrate < 1) {
        LOG_ERROR() << "video encoder: base bitrate must be positive ("
                    << base_bitrate << ")";
        return utils::Unexpected(VideoError::InvalidBitrate);
    }

    const auto bitrate = compute_bitrate(w, h, base_bitrate);

    // Above 2K resolution prefer HEVC for ~40% lower bitrate; fall back to H.264.
    constexpr int kHevcThresholdWidth = 2048;
    const bool prefer_hevc = (w > kHevcThresholdWidth);

    VideoCodec chosen_codec = VideoCodec::H264;
    const AVCodec* codec = nullptr;

    if (prefer_hevc) {
        codec = pick_hevc_encoder();
        if (codec) {
            chosen_codec = VideoCodec::HEVC;
        } else {
            LOG_WARNING() << "no HEVC encoder found, falling back to H.264";
        }
    }
    if (!codec) {
        codec = pick_h264_encoder();
        chosen_codec = VideoCodec::H264;
    }
    if (!codec) {
        LOG_ERROR() << "no video encoder found";
        return utils::Unexpected(VideoError::EncoderInitFailed);
    }

    LOG_INFO() << "selected video encoder: " << codec->name;

    shutdown();

    auto ctx = try_open_encoder(codec, w, h, f, bitrate);
    if (!ctx) {
        const std::string_view name { codec->name };
        const bool is_sw = name == "libx264" || name == "libx265";
        if (is_sw) {
            LOG_ERROR() << "failed to open " << name << " encoder";
            return utils::Unexpected(VideoError::EncoderInitFailed);
        }

        // HW encoder failed: try the matching SW fallback, then H.264 if on HEVC path.
        if (chosen_codec == VideoCodec::HEVC) {
            LOG_WARNING() << codec->name << " failed, trying libx265";
            codec = try_encoder("libx265");
            if (codec) {
                ctx = try_open_encoder(codec, w, h, f, bitrate);
            }
        }
        if (!ctx) {
            LOG_WARNING() << "falling back to libx264";
            chosen_codec = VideoCodec::H264;
            codec = try_encoder("libx264");
            if (!codec) {
                codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            }
            if (codec) {
                ctx = try_open_encoder(codec, w, h, f, bitrate);
            }
        }
        if (!ctx) {
            LOG_ERROR() << "all video encoder fallbacks failed";
            return utils::Unexpected(VideoError::EncoderInitFailed);
        }
    }

    auto frame = ff::FramePtr { av_frame_alloc() };
    if (!frame) {
        LOG_ERROR() << "av_frame_alloc failed";
        return utils::Unexpected(VideoError::EncoderInitFailed);
    }
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = w;
    frame->height = h;
    if (const int ret = av_frame_get_buffer(frame.get(), 0); ret < 0) {
        LOG_ERROR() << "av_frame_get_buffer failed: " << ff_err(ret);
        return utils::Unexpected(VideoError::EncoderInitFailed);
    }

    auto pkt = ff::PacketPtr { av_packet_alloc() };
    if (!pkt) {
        LOG_ERROR() << "av_packet_alloc failed";
        return utils::Unexpected(VideoError::EncoderInitFailed);
    }

    auto sws = ff::SwsContextPtr {
        sws_getContext(w, h, AV_PIX_FMT_BGRA, w, h, AV_PIX_FMT_YUV420P,
            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr)
    };
    if (!sws) {
        LOG_ERROR() << "sws_getContext (encoder) failed";
        return utils::Unexpected(VideoError::EncoderInitFailed);
    }

    ctx_ = std::move(ctx);
    frame_ = std::move(frame);
    pkt_ = std::move(pkt);
    sws_ = std::move(sws);

    state_ = State {
        .base_bitrate_kbps = base_bitrate,
        .fps = f,
        .width = w,
        .height = h,
        .pts = 0,
        .codec = chosen_codec,
    };

    bytes_since_calc_ = 0;
    last_calc_ = utils::Now();
    measured_kbps_ = 0;

    LOG_INFO() << "video encoder: " << w << "x" << h << " @ " << f << " fps"
               << " " << utils::to_string(chosen_codec)
               << " (" << codec->name << ") bitrate=" << bitrate << " kbps"
               << " gop=" << ctx_->gop_size;
    return { };
}

void VideoEncoder::shutdown()
{
    ctx_.reset();
    sws_.reset();
    frame_.reset();
    pkt_.reset();
    state_ = { };
}

const std::vector<uint8_t>&
VideoEncoder::encode(const std::vector<uint8_t>& bgra, int width, int height)
{
    encode_buf_.clear();

    if (!ctx_ || width != state_.width || height != state_.height) {
        return encode_buf_;
    }

    av_frame_make_writable(frame_.get());

    const uint8_t* src_slices[1] = { bgra.data() };
    int src_stride[1] = { width * 4 };
    sws_scale(sws_.get(), src_slices, src_stride, 0, height, frame_->data,
        frame_->linesize);

    frame_->pts = static_cast<int64_t>(state_.pts++);
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
        const auto now = utils::Now();
        const auto elapsed_ms = utils::ElapsedMs(last_calc_, now);

        bytes_since_calc_ += encode_buf_.size();
        if (elapsed_ms >= 1000) {
            measured_kbps_ = static_cast<int>(bytes_since_calc_ * 8 / elapsed_ms);
            bytes_since_calc_ = 0;
            last_calc_ = now;
        }
    }

    return encode_buf_;
}

// --- VideoDecoder -----------------------------------------------------------

bool VideoDecoder::init(VideoCodec video_codec)
{
    shutdown();

    const bool use_hevc = (video_codec == VideoCodec::HEVC);
    const utils::vector_view<const HWDecoderSpec> hw_specs = use_hevc
        ? utils::vector_view<const HWDecoderSpec>(kHWDecodersHEVC, std::size(kHWDecodersHEVC))
        : utils::vector_view<const HWDecoderSpec>(kHWDecodersH264, std::size(kHWDecodersH264));

    // Try hardware decoders first, in platform-specific priority order.
    for (const auto& spec : hw_specs) {
        const AVCodec* codec = try_decoder(spec.codec_name);
        if (!codec) {
            continue;
        }

        ff::CodecContextPtr ctx { avcodec_alloc_context3(codec) };
        if (!ctx) {
            continue;
        }

        // HW decoders don't benefit from SW thread parallelism.
        ctx->thread_count = 1;
        // Required by CUVID and some other HW decoders to handle PTS correctly.
        // We don't use decoder-side timestamps, so any valid rational works.
        ctx->pkt_timebase = { 1, 90000 };

        AVBufferRef* hw_dev = nullptr;
        if (spec.hw_type != AV_HWDEVICE_TYPE_NONE) {
            if (av_hwdevice_ctx_create(&hw_dev, spec.hw_type,
                    nullptr, nullptr, 0)
                < 0) {
                LOG_WARNING() << "hw device create failed for " << spec.codec_name;
                continue;
            }
            ctx->hw_device_ctx = av_buffer_ref(hw_dev);
        }

        if (avcodec_open2(ctx.get(), codec, nullptr) < 0) {
            LOG_WARNING() << "avcodec_open2 (" << spec.codec_name << ") failed";
            av_buffer_unref(&hw_dev);
            continue;
        }

        ctx_ = std::move(ctx);
        frame_ = ff::FramePtr { av_frame_alloc() };
        sw_frame_ = ff::FramePtr { av_frame_alloc() };
        pkt_ = ff::PacketPtr { av_packet_alloc() };
        hw_device_ctx_ = hw_dev;
        is_hw_ = true;

        LOG_INFO() << "video decoder: " << (use_hevc ? "HEVC" : "H.264")
                   << " (" << spec.codec_name << ") [hardware]";
        return true;
    }

    // Fallback: software decoder.
    const AVCodecID sw_id = use_hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
    const AVCodec* codec = avcodec_find_decoder(sw_id);
    if (!codec) {
        LOG_ERROR() << (use_hevc ? "HEVC" : "H.264") << " decoder not found";
        return false;
    }

    ff::CodecContextPtr ctx { avcodec_alloc_context3(codec) };
    if (!ctx) {
        LOG_ERROR() << "avcodec_alloc_context3 (decoder) failed";
        return false;
    }
    ctx->thread_count = optimal_thread_count();
    ctx->thread_type = FF_THREAD_SLICE;

    if (const int ret = avcodec_open2(ctx.get(), codec, nullptr); ret < 0) {
        LOG_ERROR() << "avcodec_open2 (decoder) failed: " << ff_err(ret);
        return false;
    }

    ctx_ = std::move(ctx);
    frame_ = ff::FramePtr { av_frame_alloc() };
    pkt_ = ff::PacketPtr { av_packet_alloc() };
    is_hw_ = false;

    LOG_INFO() << "video decoder: " << (use_hevc ? "HEVC" : "H.264")
               << " (" << codec->name << ") [software]";
    return true;
}

void VideoDecoder::shutdown()
{
    ctx_.reset();
    sws_.reset();
    frame_.reset();
    sw_frame_.reset();
    pkt_.reset();
    if (hw_device_ctx_) {
        av_buffer_unref(&hw_device_ctx_);
        hw_device_ctx_ = nullptr;
    }
    last_w_ = 0;
    last_h_ = 0;
    last_fmt_ = -1;
    is_hw_ = false;
}

bool VideoDecoder::decode(const uint8_t* data,
    size_t len,
    std::vector<uint8_t>& rgba_out,
    int& out_w,
    int& out_h)
{
    if (!ctx_) {
        return false;
    }

    pkt_->data = const_cast<uint8_t*>(data);
    pkt_->size = static_cast<int>(len);

    int ret = avcodec_send_packet(ctx_.get(), pkt_.get());
    av_packet_unref(pkt_.get());
    if (ret < 0) {
        LOG_WARNING() << "avcodec_send_packet: " << ff_err(ret);
        return false;
    }

    // avcodec_receive_frame always calls av_frame_unref(frame) before returning,
    // including on EAGAIN. With max_b_frames=0 and LOW_DELAY, one send produces
    // at most one frame, so a single receive is correct here.
    if (const int rcv = avcodec_receive_frame(ctx_.get(), frame_.get()); rcv < 0) {
        // EAGAIN is normal for hardware decoders that buffer frames internally
        // (e.g. h264_cuvid). The caller must keep sending packets; a frame will
        // appear once the pipeline is primed.
        if (rcv != AVERROR(EAGAIN)) {
            LOG_WARNING() << "avcodec_receive_frame: " << ff_err(rcv);
        }
        return false;
    }

    // For hardware frames, transfer pixel data to a CPU-side buffer.
    // av_pix_fmt_desc_get flags AV_PIX_FMT_FLAG_HWACCEL for all HW formats
    // (AV_PIX_FMT_CUDA, AV_PIX_FMT_VAAPI, AV_PIX_FMT_VIDEOTOOLBOX, …).
    AVFrame* src = frame_.get();
    const auto* pix_desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame_->format));
    if (pix_desc && (pix_desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
        if (!sw_frame_) {
            return false;
        }
        if (const int tr = av_hwframe_transfer_data(sw_frame_.get(), frame_.get(), 0); tr < 0) {
            LOG_WARNING() << "av_hwframe_transfer_data: " << ff_err(tr);
            return false;
        }
        sw_frame_->width = frame_->width;
        sw_frame_->height = frame_->height;
        src = sw_frame_.get();
    }

    const int w = src->width;
    const int h = src->height;
    const int fmt = src->format;

    if (w != last_w_ || h != last_h_ || fmt != last_fmt_) {
        sws_.reset(sws_getContext(w, h, static_cast<AVPixelFormat>(fmt),
            w, h, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr));
        last_w_ = w;
        last_h_ = h;
        last_fmt_ = fmt;
    }

    if (!sws_) {
        return false;
    }

    out_w = w;
    out_h = h;
    rgba_out.resize(static_cast<size_t>(w) * h * 4);

    uint8_t* dst_slices[1] = { rgba_out.data() };
    int dst_stride[1] = { w * 4 };
    sws_scale(sws_.get(), src->data, src->linesize, 0, h, dst_slices, dst_stride);

    return true;
}
