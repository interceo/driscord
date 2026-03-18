#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "time.hpp"

struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;

class VideoEncoder {
public:
    VideoEncoder() = default;
    ~VideoEncoder();

    VideoEncoder(const VideoEncoder&)            = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    bool init(int width, int height, int fps, int base_bitrate_kbps, int gop_size = 0);
    void shutdown();

    const std::vector<uint8_t>& encode(const std::vector<uint8_t>& bgra, int width, int height);

    void force_keyframe() { force_keyframe_ = true; }

    int width() const { return width_; }
    int height() const { return height_; }
    int fps() const { return fps_; }
    int measured_kbps() const { return measured_kbps_; }

    static int compute_bitrate(int w, int h, int base_kbps);

private:
    AVCodecContext* ctx_ = nullptr;
    SwsContext* sws_     = nullptr;
    AVFrame* frame_      = nullptr;
    AVPacket* pkt_       = nullptr;
    int width_           = 0;
    int height_          = 0;
    int fps_             = 30;
    int gop_size_        = 0;
    int64_t pts_         = 0;

    std::vector<uint8_t> encode_buf_;
    std::atomic<bool> force_keyframe_{false};

    std::atomic<int> measured_kbps_{0};
    size_t bytes_since_calc_    = 0;
    utils::Timestamp last_calc_ = utils::Now();
};

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&)            = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool init();
    void shutdown();
    bool ready() const { return ctx_ != nullptr; }

    bool decode(const std::vector<uint8_t>& data, std::vector<uint8_t>& rgba_out, int& out_w, int& out_h);

private:
    AVCodecContext* ctx_ = nullptr;
    SwsContext* sws_     = nullptr;
    AVFrame* frame_      = nullptr;
    AVPacket* pkt_       = nullptr;
    int last_w_          = 0;
    int last_h_          = 0;
};
