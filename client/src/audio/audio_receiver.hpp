#pragma once

#include "audio_jitter.hpp"
#include "utils/opus_codec.hpp"
#include "utils/protocol.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ScreenStreamJitter;
struct ma_device;

class AudioReceiver {
public:
    AudioReceiver();
    explicit AudioReceiver(int jitter_ms);
    ~AudioReceiver();

    AudioReceiver(const AudioReceiver&) = delete;
    AudioReceiver& operator=(const AudioReceiver&) = delete;

    bool start();
    void stop();
    bool running() const { return running_; }

    void feed_packet(const std::string& peer_id, const uint8_t* data, size_t len,
                     float peer_volume = 1.0f);
    void remove_voice_peer(const std::string& peer_id);

    void set_screen_stream(ScreenStreamJitter* stream) { screen_stream_ = stream; }
    void set_sharing_screen_audio(bool v) { sharing_screen_audio_ = v; }

    void set_deafened(bool d) { deafened_ = d; }
    bool deafened() const noexcept { return deafened_; }

    void set_output_volume(float v) { output_volume_ = v; }
    float output_volume() const noexcept { return output_volume_; }

    float output_level() const noexcept { return output_level_; }

private:
    void on_playback(float* output, uint32_t frames);

    int jitter_ms_ = 80;

    std::atomic<bool> running_{false};
    std::atomic<bool> deafened_{false};
    std::atomic<bool> sharing_screen_audio_{false};
    std::atomic<float> output_volume_{1.0f};
    std::atomic<float> output_level_{0.0f};

    std::unique_ptr<OpusDecode> decoder_;
    std::unique_ptr<ma_device> device_;

    std::mutex voice_mutex_;
    std::unordered_map<std::string, std::shared_ptr<AudioJitter>> voice_jitters_;
    std::vector<std::shared_ptr<AudioJitter>> voice_snapshot_;
    std::vector<float> voice_mix_buf_;

    ScreenStreamJitter* screen_stream_ = nullptr;
    std::vector<float> screen_mix_buf_;

    std::vector<float> decode_buf_;
    uint64_t playback_count_ = 0;
};
