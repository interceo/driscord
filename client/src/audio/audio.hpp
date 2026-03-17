#pragma once

#include "audio_jitter.hpp"
#include "utils/opus_codec.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class MaDevice;

class AudioSender {
public:
    static constexpr int kChannels = 1;

    using PacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    AudioSender();
    ~AudioSender();

    AudioSender(const AudioSender&) = delete;
    AudioSender& operator=(const AudioSender&) = delete;

    bool start(PacketCallback on_packet);
    void stop();
    bool running() const { return running_; }

    void set_muted(bool m) { muted_ = m; }
    bool muted() const noexcept { return muted_; }

    float input_level() const noexcept { return input_level_; }

private:
    void on_capture(const float* input, uint32_t frames);

    std::atomic<bool> running_{false};
    std::atomic<bool> muted_{false};
    std::atomic<float> input_level_{0.0f};

    PacketCallback on_packet_;

    std::unique_ptr<OpusEncode> encoder_;
    std::unique_ptr<MaDevice> device_;

    std::vector<float> capture_buf_;
    size_t capture_pos_ = 0;

    std::vector<uint8_t> encode_buf_;
    uint64_t send_seq_ = 0;
};

class AudioReceiver {
public:
    explicit AudioReceiver(int jitter_ms, int channels = 1, int sample_rate = opus::kSampleRate);

    AudioReceiver(const AudioReceiver&) = delete;
    AudioReceiver& operator=(const AudioReceiver&) = delete;

    void push_packet(const uint8_t* data, size_t len);
    std::vector<float> pop();

    // Wall-clock timestamp (ms) of the last audio frame delivered to the mixer.
    // Zero until audio starts playing. Safe to read from any thread.
    uint64_t last_ts_ms() const noexcept { return jitter_.last_ts_ms(); }

    // Discard all queued audio older than ts_ms (render-thread A/V sync helper).
    size_t drain_before(uint64_t ts_ms) { return jitter_.drain_before(ts_ms); }

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

    int id_;
    uint64_t push_count_ = 0;
    uint64_t pop_count_ = 0;

    static int next_id_;
};
