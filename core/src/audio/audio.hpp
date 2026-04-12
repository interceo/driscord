#pragma once

#include "opus_codec.hpp"
#include "utils/expected.hpp"
#include "utils/jitter.hpp"
#include "utils/metrics.hpp"
#include "utils/vector_view.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class MaDevice;

enum class AudioError {
    OpusInitFailed,
    SenderDeviceStartFailed,
    MixerDeviceStartFailed,
};
const char* to_string(AudioError e);

class AudioSender {
public:
    static constexpr int kChannels = 1;

    using PacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    AudioSender();
    ~AudioSender();

    AudioSender(const AudioSender&) = delete;
    AudioSender& operator=(const AudioSender&) = delete;

    // Returns JSON array of {id, name} for all capture devices.
    static std::string list_input_devices_json();

    utils::Expected<void, AudioError> start(PacketCallback on_packet,
        int bitrate_bps = 64000);
    void stop();
    bool running() const { return running_; }

    // Set the capture device by name (empty = default). If already running,
    // restarts capture on the new device immediately.
    void set_device_id(std::string id);

    void set_muted(bool m) { muted_ = m; }
    bool muted() const noexcept { return muted_; }

    void set_noise_gate(float threshold) { noise_gate_ = threshold; }
    float noise_gate() const noexcept { return noise_gate_; }

    float input_level() const noexcept { return input_level_; }

private:
    void on_capture(const float* input, uint32_t frames);

    std::atomic<bool> running_ { false };
    std::atomic<bool> muted_ { false };
    std::atomic<float> noise_gate_ { 0.0f };
    std::atomic<float> input_level_ { 0.0f };

    std::string device_id_; // empty = default device
    int bitrate_bps_ = 64000;
    PacketCallback on_packet_;

    std::unique_ptr<OpusEncode> encoder_;
    std::unique_ptr<MaDevice> device_;

    std::vector<float> capture_buf_;
    size_t capture_pos_ = 0;

    std::vector<uint8_t> encode_buf_;
    uint64_t send_seq_ = 0;
};

// Single-peer audio receiver: one decoder, one jitter buffer.
// Per-peer lifecycle (creation, volume, mute) is managed by the caller
// (AudioTransport or ScreenReceiver). AudioMixer applies src->volume() to the
// output of pop().
//
// Raw Opus packets are stored in the jitter buffer and decoded at pop() time.
// This allows PLC (Packet Loss Concealment) on sequence gaps: the Opus decoder
// generates a concealment frame via opus_decode(NULL, 0, ...).
class AudioReceiver {
public:
    struct OpusFrame {
        std::vector<uint8_t> data; // raw Opus-encoded bytes
        utils::WallTimestamp sender_ts { };

        bool empty() const noexcept { return data.empty(); }
    };

    using AudioJitter = utils::Jitter<OpusFrame>;

    explicit AudioReceiver(int jitter_ms,
        int channels = 1,
        int sample_rate = opus::kSampleRate);

    AudioReceiver(const AudioReceiver&) = delete;
    AudioReceiver& operator=(const AudioReceiver&) = delete;

    void push_packet(utils::vector_view<const uint8_t> data);

    // Pops one decoded frame of PCM samples (mono, mixed down from channels_).
    // Returns a view into internal buffers — valid until the next pop() call.
    utils::vector_view<const float> pop();

    void set_volume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }

    void set_muted(bool m) { muted_.store(m); }
    bool muted() const { return muted_.load(); }

    // Stereo pan: 0.0 = hard left, 0.5 = center, 1.0 = hard right.
    void set_pan(float p) { pan_.store(std::clamp(p, 0.0f, 1.0f)); }
    float pan() const { return pan_.load(); }

    size_t evict_old(utils::Duration max_delay);

    std::optional<utils::WallTimestamp> front_effective_ts() const;
    bool primed() const;
    int64_t front_age_ms() const;

    void reset();

    struct Stats {
        size_t queue_size = 0;
        uint64_t drop_count = 0;
        uint64_t miss_count = 0;
        uint64_t packets_received = 0;
        uint64_t decode_errors = 0;
    };
    Stats stats() const;

private:
    std::atomic<bool> reset_pending_ { false };
    OpusDecode decoder_;
    AudioJitter jitter_;
    std::vector<float> decode_buf_;
    std::vector<float> mono_buf_;
    uint64_t push_count_ = 0;

    int channels_;
    int sample_rate_;

    std::atomic<float> volume_ { 1.0f };
    std::atomic<bool> muted_ { false };
    std::atomic<float> pan_ { 0.5f };

    uint64_t id_ = 0;
    uint64_t pop_count_ = 0;

    utils::Counter drop_count_;
    utils::Counter miss_count_;
    utils::Counter decode_error_count_;

    static std::atomic<uint64_t> next_id_;
};
