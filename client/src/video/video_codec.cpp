#include "video_codec.hpp"

#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>

#include <algorithm>
#include <cstring>

#include "log.hpp"

// --- colour-space helpers ---------------------------------------------------

static void bgra_to_i420(const uint8_t* bgra, int w, int h,
                          uint8_t* y, uint8_t* u, uint8_t* v) {
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            int px = (row * w + col) * 4;
            uint8_t b = bgra[px + 0];
            uint8_t g = bgra[px + 1];
            uint8_t r = bgra[px + 2];

            y[row * w + col] = static_cast<uint8_t>(
                std::clamp(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16, 0, 255));

            if ((row & 1) == 0 && (col & 1) == 0) {
                int uv = (row / 2) * (w / 2) + (col / 2);
                u[uv] = static_cast<uint8_t>(
                    std::clamp(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128, 0, 255));
                v[uv] = static_cast<uint8_t>(
                    std::clamp(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128, 0, 255));
            }
        }
    }
}

static void i420_to_rgba(const vpx_image_t* img, uint8_t* rgba) {
    int w = static_cast<int>(img->d_w);
    int h = static_cast<int>(img->d_h);

    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            int yv = img->planes[VPX_PLANE_Y][row * img->stride[VPX_PLANE_Y] + col];
            int uv = img->planes[VPX_PLANE_U][(row / 2) * img->stride[VPX_PLANE_U] + (col / 2)];
            int vv = img->planes[VPX_PLANE_V][(row / 2) * img->stride[VPX_PLANE_V] + (col / 2)];

            int c = yv - 16;
            int d = uv - 128;
            int e = vv - 128;

            int px = (row * w + col) * 4;
            rgba[px + 0] = static_cast<uint8_t>(std::clamp((298 * c + 409 * e + 128) >> 8, 0, 255));
            rgba[px + 1] = static_cast<uint8_t>(std::clamp((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255));
            rgba[px + 2] = static_cast<uint8_t>(std::clamp((298 * c + 516 * d + 128) >> 8, 0, 255));
            rgba[px + 3] = 255;
        }
    }
}

// --- VideoEncoder -----------------------------------------------------------

VideoEncoder::~VideoEncoder() { shutdown(); }

bool VideoEncoder::init(int width, int height, int bitrate_kbps) {
    shutdown();

    if (width % 2 != 0 || height % 2 != 0) {
        LOG_ERROR() << "video encoder: dimensions must be even (" << width << "x" << height << ")";
        return false;
    }

    vpx_codec_enc_cfg_t cfg;
    if (vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK) {
        LOG_ERROR() << "vpx_codec_enc_config_default failed";
        return false;
    }

    cfg.g_w = static_cast<unsigned>(width);
    cfg.g_h = static_cast<unsigned>(height);
    cfg.rc_target_bitrate = static_cast<unsigned>(bitrate_kbps);
    cfg.g_timebase.num = 1;
    cfg.g_timebase.den = 1000;
    cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;
    cfg.g_lag_in_frames = 0;
    cfg.rc_end_usage = VPX_CBR;
    cfg.g_threads = 2;
    cfg.kf_max_dist = 150;

    if (vpx_codec_enc_init(&codec_, vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK) {
        LOG_ERROR() << "vpx_codec_enc_init failed: " << vpx_codec_error(&codec_);
        return false;
    }

    vpx_codec_control(&codec_, VP8E_SET_CPUUSED, 8);
    vpx_codec_control(&codec_, VP8E_SET_NOISE_SENSITIVITY, 0);

    image_ = vpx_img_alloc(nullptr, VPX_IMG_FMT_I420,
                            static_cast<unsigned>(width), static_cast<unsigned>(height), 1);
    if (!image_) {
        LOG_ERROR() << "vpx_img_alloc failed";
        vpx_codec_destroy(&codec_);
        return false;
    }

    width_ = width;
    height_ = height;
    pts_ = 0;
    initialized_ = true;

    LOG_INFO() << "video encoder initialised: " << width << "x" << height
               << " @ " << bitrate_kbps << " kbps";
    return true;
}

void VideoEncoder::shutdown() {
    if (!initialized_) return;
    vpx_img_free(image_);
    image_ = nullptr;
    vpx_codec_destroy(&codec_);
    initialized_ = false;
}

std::vector<uint8_t> VideoEncoder::encode(const uint8_t* bgra, int width, int height) {
    if (!initialized_ || width != width_ || height != height_) return {};

    bgra_to_i420(bgra, width, height,
                 image_->planes[VPX_PLANE_Y],
                 image_->planes[VPX_PLANE_U],
                 image_->planes[VPX_PLANE_V]);

    vpx_codec_err_t err = vpx_codec_encode(&codec_, image_, pts_, 1, 0, VPX_DL_REALTIME);
    ++pts_;

    if (err != VPX_CODEC_OK) {
        LOG_ERROR() << "vpx_codec_encode failed: " << vpx_codec_error(&codec_);
        return {};
    }

    std::vector<uint8_t> result;
    vpx_codec_iter_t iter = nullptr;
    const vpx_codec_cx_pkt_t* pkt;
    while ((pkt = vpx_codec_get_cx_data(&codec_, &iter)) != nullptr) {
        if (pkt->kind == VPX_CODEC_CX_FRAME_PKT) {
            auto* p = static_cast<const uint8_t*>(pkt->data.frame.buf);
            result.insert(result.end(), p, p + pkt->data.frame.sz);
        }
    }
    return result;
}

// --- VideoDecoder -----------------------------------------------------------

VideoDecoder::~VideoDecoder() { shutdown(); }

bool VideoDecoder::init() {
    shutdown();
    vpx_codec_dec_cfg_t cfg{};
    cfg.threads = 2;

    if (vpx_codec_dec_init(&codec_, vpx_codec_vp8_dx(), &cfg, 0) != VPX_CODEC_OK) {
        LOG_ERROR() << "vpx_codec_dec_init failed: " << vpx_codec_error(&codec_);
        return false;
    }
    initialized_ = true;
    return true;
}

void VideoDecoder::shutdown() {
    if (!initialized_) return;
    vpx_codec_destroy(&codec_);
    initialized_ = false;
}

bool VideoDecoder::decode(const uint8_t* data, size_t len,
                          std::vector<uint8_t>& rgba_out, int& out_w, int& out_h) {
    if (!initialized_) return false;

    if (vpx_codec_decode(&codec_, data, static_cast<unsigned>(len), nullptr, 0) != VPX_CODEC_OK) {
        LOG_ERROR() << "vpx_codec_decode failed: " << vpx_codec_error(&codec_);
        return false;
    }

    vpx_codec_iter_t iter = nullptr;
    vpx_image_t* img = vpx_codec_get_frame(&codec_, &iter);
    if (!img) return false;

    out_w = static_cast<int>(img->d_w);
    out_h = static_cast<int>(img->d_h);
    rgba_out.resize(static_cast<size_t>(out_w) * out_h * 4);
    i420_to_rgba(img, rgba_out.data());
    return true;
}
