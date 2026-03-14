#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;

class VideoEncoder {
public:
    VideoEncoder() = default;
    ~VideoEncoder();

    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    bool init(int width, int height, int bitrate_kbps);
    bool reinit(int width, int height, int bitrate_kbps);
    void shutdown();

    std::vector<uint8_t> encode(const uint8_t* bgra, int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

private:
    AVCodecContext* ctx_ = nullptr;
    SwsContext* sws_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* pkt_ = nullptr;
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

    bool decode(const uint8_t* data, size_t len,
                std::vector<uint8_t>& rgba_out, int& out_w, int& out_h);

private:
    AVCodecContext* ctx_ = nullptr;
    SwsContext* sws_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* pkt_ = nullptr;
    int last_w_ = 0;
    int last_h_ = 0;
};
