#pragma once

#include "utils/jitter.hpp"
#include "utils/opus_codec.hpp"
#include "utils/vector_view.hpp"

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

    void push_packet(const utils::vector_view<const uint8_t> data);
    std::vector<float> pop();

    void set_volume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }

    // Discard all queued audio older than max_delay (hard timeout).
    size_t evict_old(utils::Duration max_delay) { return jitter_.evict_old(max_delay); }

    // A/V sync helpers.
    std::optional<utils::WallTimestamp> front_effective_ts() const {
        return jitter_.front_effective_ts();
    }
    bool primed() const { return jitter_.primed(); }

    int64_t front_age_ms() const { return jitter_.front_age_ms(); }

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
    uint64_t pop_count_  = 0;

    static std::atomic<int> next_id_;
};
