#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace opus {

constexpr int kSampleRate = 48000;
constexpr int kFrameSize = 960; // 20ms @ 48kHz
constexpr int kMaxPacket = 4000;
} // namespace opus

struct OpusEncoder;
struct OpusDecoder;

class OpusEncode {
public:
    OpusEncode() = default;
    ~OpusEncode();

    OpusEncode(const OpusEncode&) = delete;
    OpusEncode& operator=(const OpusEncode&) = delete;

    bool init(const size_t sample_rate,
        const size_t channels,
        const size_t bitrate,
        const size_t application);
    void shutdown();

    int encode(const float* pcm,
        const size_t frame_size,
        uint8_t* output,
        const size_t max_output);

    size_t sample_rate() const { return sample_rate_; }
    size_t channels() const { return channels_; }

private:
    OpusEncoder* encoder_ = nullptr;
    size_t sample_rate_ = 0;
    size_t channels_ = 0;
};

class OpusDecode {
public:
    OpusDecode() = default;
    ~OpusDecode();

    OpusDecode(const OpusDecode&) = delete;
    OpusDecode& operator=(const OpusDecode&) = delete;

    bool init(int sample_rate, int channels);
    void shutdown();

    int decode(const uint8_t* data,
        const size_t len,
        float* output,
        const size_t max_samples);

    size_t sample_rate() const { return sample_rate_; }
    size_t channels() const { return channels_; }

private:
    OpusDecoder* decoder_ = nullptr;
    size_t sample_rate_ = 0;
    size_t channels_ = 0;
};
