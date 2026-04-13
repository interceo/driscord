#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "time.hpp"
#include "utils/expected.hpp"
#include "utils/protocol.hpp"
#include "vector_view.hpp"

using protocol::VideoCodec;

enum class VideoError {
    InvalidDimensions,
    InvalidFps,
    InvalidBitrate,
    EncoderInitFailed,
    VideoSenderFailed,
    CaptureStartFailed,
};
const char* to_string(VideoError e);

struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;

// Custom deleters for FFmpeg opaque types — defined in video_codec.cpp to keep
// FFmpeg headers out of this header.
namespace ff {
struct CodecContextDeleter {
    void operator()(AVCodecContext*) const noexcept;
};
struct SwsContextDeleter {
    void operator()(SwsContext*) const noexcept;
};
struct FrameDeleter {
    void operator()(AVFrame*) const noexcept;
};
struct PacketDeleter {
    void operator()(AVPacket*) const noexcept;
};

using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

} // namespace ff

class VideoEncoder {
public:
    VideoEncoder() = default;
    ~VideoEncoder() = default;

    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    utils::Expected<void, VideoError> init(size_t width, size_t height, size_t fps, size_t base_bitrate_kbps);
    void shutdown();

    const std::vector<uint8_t>& encode(const std::vector<uint8_t>& bgra,
        int width,
        int height);

    void force_keyframe() { force_keyframe_ = true; }

    int width() const { return state_.width; }
    int height() const { return state_.height; }
    int fps() const { return state_.fps; }
    int measured_kbps() const { return measured_kbps_; }
    VideoCodec codec() const { return state_.codec; }

private:
    ff::CodecContextPtr ctx_;
    ff::SwsContextPtr sws_;
    ff::FramePtr frame_;
    ff::PacketPtr pkt_;

    struct State {
        int base_bitrate_kbps = 0;
        int fps = 0;
        int width = 0;
        int height = 0;
        uint64_t pts = 0;
        VideoCodec codec = VideoCodec::H264;
    } state_;

    std::vector<uint8_t> encode_buf_;
    std::atomic<bool> force_keyframe_ { false };

    utils::Timestamp last_calc_ = { };
    std::atomic<int> measured_kbps_ = 0;
    std::atomic<size_t> bytes_since_calc_ = 0;
};

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder() = default;

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool init(VideoCodec codec = VideoCodec::H264);
    void shutdown();
    bool ready() const { return ctx_ != nullptr; }
    bool is_hw() const { return is_hw_; }

    bool decode(const uint8_t* data,
        size_t len,
        std::vector<uint8_t>& rgba_out,
        int& out_w,
        int& out_h);

private:
    ff::CodecContextPtr ctx_;
    ff::SwsContextPtr sws_;
    ff::FramePtr frame_;
    ff::FramePtr sw_frame_; // scratch buffer for HW→CPU transfer
    ff::PacketPtr pkt_;
    AVBufferRef* hw_device_ctx_ = nullptr;
    int last_w_ = 0;
    int last_h_ = 0;
    int last_fmt_ = -1; // AV_PIX_FMT of the sw-side frame, for sws rebuild
    bool is_hw_ = false;
};
