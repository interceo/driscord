#pragma once

#include "audio_jitter.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ScreenStreamJitter;

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

    using PacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    explicit AudioEngine(int voice_jitter_ms = 80);
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool start(PacketCallback on_packet);
    void stop();
    bool running() const { return running_; }

    void feed_packet(const std::string& peer_id, const uint8_t* data, size_t len, float peer_volume = 1.0f);
    void remove_voice_peer(const std::string& peer_id);

    void set_screen_stream(ScreenStreamJitter* stream) { screen_stream_ = stream; }
    void set_sharing_screen_audio(bool v) { sharing_screen_audio_ = v; }

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

    int voice_jitter_ms_;

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

    std::mutex voice_mutex_;
    std::unordered_map<std::string, std::shared_ptr<AudioJitter>> voice_jitters_;
    std::vector<std::shared_ptr<AudioJitter>> voice_snapshot_;
    std::vector<float> voice_mix_buf_;

    ScreenStreamJitter* screen_stream_ = nullptr;
    std::vector<float> screen_mix_buf_;

    std::vector<uint8_t> encode_buf_;
    std::vector<float> decode_buf_;

    uint16_t voice_send_seq_ = 0;
    uint64_t playback_count_ = 0;
};
