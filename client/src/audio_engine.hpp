#pragma once

#include "ring_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;
struct ma_device;

class AudioEngine {
public:
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNELS = 1;
    static constexpr int FRAME_SIZE = 960;   // 20ms @ 48kHz
    static constexpr int MAX_OPUS_PACKET = 4000;

    using PacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool start(PacketCallback on_packet);
    void stop();
    bool running() const { return running_; }

    void feed_packet(const uint8_t* data, size_t len);

    void set_muted(bool m) { muted_ = m; }
    bool muted() const { return muted_; }

    void set_output_volume(float v) { output_volume_ = v; }
    float output_volume() const { return output_volume_; }

    float input_level() const { return input_level_; }
    float output_level() const { return output_level_; }

private:
    void on_capture(const float* input, uint32_t frames);
    void on_playback(float* output, uint32_t frames);

    static void audio_callback(void* device, void* output, const void* input, uint32_t frames);

    std::atomic<bool> running_{false};
    std::atomic<bool> muted_{false};
    std::atomic<float> output_volume_{1.0f};
    std::atomic<float> input_level_{0.0f};
    std::atomic<float> output_level_{0.0f};

    PacketCallback on_packet_;

    OpusEncoder* encoder_ = nullptr;
    OpusDecoder* decoder_ = nullptr;

    std::unique_ptr<ma_device> device_;

    // Accumulator for capture frames until we have a full Opus frame
    std::vector<float> capture_buf_;
    size_t capture_pos_ = 0;

    // Ring buffer for decoded playback samples
    RingBuffer<float> playback_ring_{SAMPLE_RATE * 2};

    // Temp buffers to avoid allocations in hot path
    std::vector<uint8_t> encode_buf_;
    std::vector<float> decode_buf_;
};
