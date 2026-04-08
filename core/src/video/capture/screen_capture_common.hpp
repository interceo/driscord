#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

extern "C" {
#include <libavutil/error.h>
}

inline void scale_nearest(const uint8_t* src,
    int sw,
    int sh,
    uint8_t* dst,
    int dw,
    int dh)
{
    for (int y = 0; y < dh; ++y) {
        int sy = y * sh / dh;
        for (int x = 0; x < dw; ++x) {
            int sx = x * sw / dw;
            const uint8_t* sp = src + (sy * sw + sx) * 4;
            uint8_t* dp = dst + (y * dw + x) * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
}

inline void compute_output_size(int src_w,
    int src_h,
    int max_w,
    int max_h,
    int& out_w,
    int& out_h)
{
    out_w = src_w;
    out_h = src_h;
    if (out_w > max_w || out_h > max_h) {
        float scale = std::min(static_cast<float>(max_w) / out_w,
            static_cast<float>(max_h) / out_h);
        out_w = static_cast<int>(out_w * scale) & ~1;
        out_h = static_cast<int>(out_h * scale) & ~1;
    } else {
        out_w &= ~1;
        out_h &= ~1;
    }
    if (out_w <= 0) {
        out_w = 2;
    }
    if (out_h <= 0) {
        out_h = 2;
    }
}

inline std::string ff_err(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] { };
    av_strerror(errnum, buf, sizeof(buf));
    return buf;
}
