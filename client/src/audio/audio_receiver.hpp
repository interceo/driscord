#pragma once

#include "audio_jitter.hpp"
#include "utils/opus_codec.hpp"
#include "utils/protocol.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

class AudioReceiver {
public:
    explicit AudioReceiver(int jitter_ms, int channels = 1, int sample_rate = opus::kSampleRate);

    AudioReceiver(const AudioReceiver&) = delete;
    AudioReceiver& operator=(const AudioReceiver&) = delete;

    void push_packet(const uint8_t* data, size_t len);
    std::vector<float> pop();

    void set_volume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }

    size_t buffered_ms() const { return jitter_.buffered_ms(); }
    void reset() { jitter_.reset(); }

    using Stats = AudioJitter::Stats;
    Stats stats() const { return jitter_.stats(); }

private:
    AudioJitter jitter_;
    OpusDecode decoder_;
    int channels_;
    std::vector<float> decode_buf_;
    std::vector<float> mono_buf_;
    std::atomic<float> volume_{1.0f};
};
