#pragma once

#include "audio_jitter.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;
struct ma_device;

struct OpusEncoderDeleter {
    void operator()(OpusEncoder* e) const;
};
struct OpusDecoderDeleter {
    void operator()(OpusDecoder* d) const;
};

using OpusEncoderPtr = std::unique_ptr<OpusEncoder, OpusEncoderDeleter>;
using OpusDecoderPtr = std::unique_ptr<OpusDecoder, OpusDecoderDeleter>;

class AudioEngine {
public:
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNELS = 1;
    static constexpr int FRAME_SIZE = 960;  // 20ms @ 48kHz
    static constexpr int MAX_OPUS_PACKET = 4000;
    static constexpr size_t AUDIO_HEADER_SIZE = 6;  // seq(2) + timestamp(4)

    static constexpr int SCREEN_AUDIO_CHANNELS = 2;
    static constexpr int SCREEN_AUDIO_BITRATE = 128000;

    using PacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool start(PacketCallback on_packet);
    void stop();
    bool running() const { return running_; }

    void feed_packet(const uint8_t* data, size_t len, float peer_volume = 1.0f);

    bool init_screen_audio(PacketCallback on_screen_audio_packet);
    void shutdown_screen_audio();
    void feed_screen_audio_pcm(const float* samples, size_t frames, int channels);
    void feed_screen_audio_packet(const uint8_t* data, size_t len);

    void set_muted(bool m) { muted_ = m; }
    bool muted() const noexcept { return muted_; }

    void set_deafened(bool d) {
        deafened_ = d;
        if (d) {
            muted_ = true;
        }
    }
    bool deafened() const noexcept { return deafened_; }

    void set_output_volume(float v) { output_volume_ = v; }
    float output_volume() const noexcept { return output_volume_; }

    float input_level() const noexcept { return input_level_; }
    float output_level() const noexcept { return output_level_; }

private:
    void on_capture(const float* input, uint32_t frames);
    void on_playback(float* output, uint32_t frames);

    static void audio_callback(void* device, void* output, const void* input, uint32_t frames);

    std::atomic<bool> running_{false};
    std::atomic<bool> muted_{false};
    std::atomic<bool> deafened_{false};
    std::atomic<bool> sharing_screen_audio_{false};
    std::atomic<float> output_volume_{1.0f};
    std::atomic<float> input_level_{0.0f};
    std::atomic<float> output_level_{0.0f};

    PacketCallback on_packet_;

    OpusEncoderPtr encoder_;
    OpusDecoderPtr decoder_;

    std::unique_ptr<ma_device> device_;

    std::vector<float> capture_buf_;
    size_t capture_pos_ = 0;

    AudioJitter voice_jitter_;
    AudioJitter screen_jitter_;
    std::vector<float> screen_mix_buf_;

    std::vector<uint8_t> encode_buf_;
    std::vector<float> decode_buf_;

    uint16_t voice_send_seq_ = 0;
    uint16_t screen_send_seq_ = 0;

    OpusEncoderPtr screen_encoder_;
    OpusDecoderPtr screen_decoder_;
    PacketCallback on_screen_audio_packet_;
    std::vector<float> screen_capture_buf_;
    size_t screen_capture_pos_ = 0;
    std::vector<uint8_t> screen_encode_buf_;
    std::vector<float> screen_decode_buf_;
};
