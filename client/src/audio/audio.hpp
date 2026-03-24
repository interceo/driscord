#pragma once

#include "utils/jitter.hpp"
#include "utils/opus_codec.hpp"
#include "utils/vector_view.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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

    // Single-peer push (voice chat — maps to peer_id "").
    void push_packet(const utils::vector_view<const uint8_t> data);
    // Multi-peer push (screen audio — each peer gets its own decoder + jitter).
    void push_packet(const std::string& peer_id, const utils::vector_view<const uint8_t> data);

    // Pops and mixes samples from all active peer buffers.
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
        std::unordered_map<std::string, AudioJitter::Stats> peers;
    };
    Stats stats() const;

private:
    // Per-peer decoder + jitter, mirroring VideoReceiver::PeerDecoder.
    struct PeerBuffer {
        OpusDecode decoder;
        AudioJitter jitter;
        utils::Timestamp last_packet{};
        std::vector<float> decode_buf;
        std::vector<float> mono_buf;
        uint64_t push_count = 0;

        PeerBuffer(utils::Duration buf_delay, int channels, int sample_rate);
    };

    std::shared_ptr<PeerBuffer> get_or_create_peer(const std::string& peer_id);
    void do_push(PeerBuffer& pb, const utils::vector_view<const uint8_t> data);

    mutable std::mutex peer_mutex_;
    std::unordered_map<std::string, std::shared_ptr<PeerBuffer>> peer_buffers_;

    utils::Duration buf_delay_;
    int channels_;
    int sample_rate_;

    std::atomic<float> volume_{1.0f};
    std::atomic<bool>  muted_{false};

    int id_;
    uint64_t pop_count_ = 0;

    static std::atomic<int> next_id_;
};
