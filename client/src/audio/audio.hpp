#pragma once

#include "utils/jitter.hpp"
#include "utils/opus_codec.hpp"
#include "utils/vector_view.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

class MaDevice;

class AudioSender {
public:
    static constexpr int kChannels = 1;

    using PacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    AudioSender();
    ~AudioSender();

    AudioSender(const AudioSender&)            = delete;
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

// Single-peer audio receiver: one decoder, one jitter buffer.
// Per-peer lifecycle (creation, volume, mute) is managed by the caller (AudioTransport or
// ScreenReceiver). AudioMixer applies src->volume() to the output of pop().
class AudioReceiver {
public:
    struct PcmFrame {
        std::vector<float> samples;
        utils::WallTimestamp sender_ts{};

        bool empty() const noexcept { return samples.empty(); }
    };

    using AudioJitter = utils::Jitter<PcmFrame>;

    explicit AudioReceiver(int jitter_ms, int channels = 1, int sample_rate = opus::kSampleRate);

    AudioReceiver(const AudioReceiver&)            = delete;
    AudioReceiver& operator=(const AudioReceiver&) = delete;

    void push_packet(utils::vector_view<const uint8_t> data);

    // Pops one decoded frame of PCM samples (mono, mixed down from channels_).
    std::vector<float> pop();

    void set_volume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }

    void set_muted(bool m) { muted_.store(m); }
    bool muted() const { return muted_.load(); }

    size_t evict_old(utils::Duration max_delay);

    std::optional<utils::WallTimestamp> front_effective_ts() const;
    bool primed() const;
    int64_t front_age_ms() const;

    void reset();

    struct Stats {
        size_t   queue_size = 0;
        uint64_t drop_count = 0;
        uint64_t miss_count = 0;
    };
    Stats stats() const;

private:
    mutable std::mutex decode_mutex_;
    OpusDecode         decoder_;
    AudioJitter        jitter_;
    std::vector<float> decode_buf_;
    std::vector<float> mono_buf_;
    uint64_t           push_count_ = 0;

    int channels_;
    int sample_rate_;

    std::atomic<float> volume_{1.0f};
    std::atomic<bool>  muted_{false};

    uint64_t      id_ = 0;
    uint64_t pop_count_ = 0;

    static std::atomic<uint64_t> next_id_;
};
