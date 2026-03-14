#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vpx/vpx_decoder.h>
#include <vpx/vpx_encoder.h>

class VideoEncoder {
public:
    VideoEncoder() = default;
    ~VideoEncoder();

    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    bool init(int width, int height, int bitrate_kbps = 1500);
    void shutdown();

    // Encodes a BGRA frame, returns VP8 bitstream (empty on failure / no output).
    std::vector<uint8_t> encode(const uint8_t* bgra, int width, int height);

private:
    vpx_codec_ctx_t codec_{};
    vpx_image_t* image_ = nullptr;
    bool initialized_ = false;
    int width_ = 0;
    int height_ = 0;
    int64_t pts_ = 0;
};

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool init();
    void shutdown();

    // Decodes VP8 bitstream into RGBA pixels. Returns false on failure.
    bool decode(const uint8_t* data, size_t len,
                std::vector<uint8_t>& rgba_out, int& out_w, int& out_h);

private:
    vpx_codec_ctx_t codec_{};
    bool initialized_ = false;
};
