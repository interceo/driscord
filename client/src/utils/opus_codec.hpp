#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace opus {

constexpr int kSampleRate = 48000;
constexpr int kFrameSize = 960;  // 20ms @ 48kHz
constexpr int kMaxPacket = 4000;

}  // namespace opus

struct OpusEncoder;
struct OpusDecoder;

class OpusEncode {
public:
    OpusEncode() = default;
    ~OpusEncode();

    OpusEncode(const OpusEncode&) = delete;
    OpusEncode& operator=(const OpusEncode&) = delete;

    bool init(int sample_rate, int channels, int bitrate, int application);
    void shutdown();

    int encode(const float* pcm, int frame_size, uint8_t* output, int max_output);

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }

private:
    OpusEncoder* encoder_ = nullptr;
    int sample_rate_ = 0;
    int channels_ = 0;
};

class OpusDecode {
public:
    OpusDecode() = default;
    ~OpusDecode();

    OpusDecode(const OpusDecode&) = delete;
    OpusDecode& operator=(const OpusDecode&) = delete;

    bool init(int sample_rate, int channels);
    void shutdown();

    int decode(const uint8_t* data, int len, float* output, int max_samples);

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }

private:
    OpusDecoder* decoder_ = nullptr;
    int sample_rate_ = 0;
    int channels_ = 0;
};
