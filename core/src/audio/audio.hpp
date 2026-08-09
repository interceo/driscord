#pragma once

#include "opus_codec.hpp"
#include "sync/media_clock.hpp"
#include "utils/expected.hpp"
#include "utils/metrics.hpp"
#include "utils/mono_clock.hpp"
#include "utils/reorder_buffer.hpp"
#include "utils/spinlock.hpp"
#include "wsola.hpp"

#include <boost/circular_buffer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

class MaDevice;

enum class AudioError {
    OpusInitFailed,
    SenderDeviceStartFailed,
    MixerDeviceStartFailed,
};

class AudioSender {
public:
    static constexpr int kChannels = 1;

    using PacketCallback = std::function<void(const uint8_t* data, size_t len)>;

    AudioSender();
    ~AudioSender();

    AudioSender(const AudioSender&) = delete;
    AudioSender& operator=(const AudioSender&) = delete;

    static std::string list_input_devices_json();

    utils::Expected<void, AudioError> start(PacketCallback on_packet,
        int bitrate_bps = 64000);
    void stop();
    bool running() const { return running_; }

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
    uint32_t send_seq_ = 0;
    bool in_silence_ = true;
};

class AudioReceiver {
public:
    AudioReceiver(std::shared_ptr<avsync::MediaClock> clock,
        int channels = 1,
        int sample_rate = opus::kSampleRate,
        const utils::TimeSource& time = utils::system_time_source());
    ~AudioReceiver();

    AudioReceiver(const AudioReceiver&) = delete;
    AudioReceiver& operator=(const AudioReceiver&) = delete;

    void push_packet(std::span<const uint8_t> data);

    size_t read(float* out, size_t frames);

    void set_volume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }

    void set_muted(bool m) { muted_.store(m); }
    bool muted() const { return muted_.load(); }

    void set_pan(float p) { pan_.store(std::clamp(p, 0.0f, 1.0f)); }
    float pan() const { return pan_.load(); }

    void set_wait_for_video(bool wait) { wait_for_video_.store(wait); }
    bool wait_for_video() const { return wait_for_video_.load(); }

    const std::shared_ptr<avsync::MediaClock>& clock() const { return clock_; }

    void reset();

    struct Stats {
        size_t queue_size = 0;
        uint64_t packets_received = 0;
        uint64_t drop_count = 0; // late, duplicate, or oversized on arrival
        uint64_t conceal_count = 0; // gaps filled by Opus PLC
        uint64_t fec_count = 0; // gaps recovered from the next packet's FEC
        uint64_t underrun_count = 0; // callbacks that had to emit silence
        uint64_t decode_errors = 0;
        uint64_t stretch_count = 0; // time-scale corrections applied
        uint64_t resync_count = 0; // playout position re-established
        // Sample-level totals. Rates taken from these are what NetEq reports as
        // expand_rate / accelerate_rate / preemptive_rate; the event counts
        // above cannot distinguish one long stall from many short ones.
        uint64_t total_samples_out = 0;
        uint64_t conceal_samples = 0;
        uint64_t fec_samples = 0;
        uint64_t silence_samples = 0;
        uint64_t stretch_in_samples = 0;
        uint64_t stretch_out_samples = 0;
        int64_t target_delay_ms = 0;
        int64_t actual_delay_ms = 0;
        int64_t p50_delay_ms = -1;
        int64_t p95_delay_ms = -1;
        int64_t p99_delay_ms = -1;
        uint64_t delay_samples = 0;
        int64_t playout_ts_us = 0;
    };
    Stats stats() const;

private:
    static constexpr size_t kMaxStoredPacket = 512;
    static constexpr size_t kBufferCapacity = 128;
    static constexpr size_t kStageCapacity = 4096;
    static constexpr size_t kRingCapacity = 16384;
    static constexpr int kMaxConsecutiveConceals = 25;
    static constexpr int64_t kMaxStretchBudgetUs = 30'000;

    struct Packet {
        uint16_t len = 0;
        uint32_t flags = 0;
        int64_t sender_ts_us = 0;
        std::array<uint8_t, kMaxStoredPacket> data { };
    };

    bool playout_step();
    bool decode_into(std::vector<float>& dst, int64_t* sender_ts_us = nullptr);
    bool next_packet_ready() const;
    bool blocked_by_video(int64_t sender_ts_us) const;
    void refill_stretch_budget(int64_t now);

    std::shared_ptr<avsync::MediaClock> clock_;
    const utils::TimeSource* time_;

    mutable utils::SpinLock buffer_lock_;
    utils::ReorderBuffer<Packet, kBufferCapacity> buffer_;

    OpusDecode decoder_;
    Wsola wsola_;
    boost::circular_buffer<float> ring_;

    bool primed_ = false;
    bool pending_decoder_reset_ = false;
    int64_t next_ts_us_ = 0; // sender timestamp about to be played
    int consecutive_conceals_ = 0;
    int64_t stretch_budget_us_ = 0; // token bucket limiting time-stretch rate
    int64_t budget_updated_us_ = 0;
    int64_t actual_delay_us_ = 0;

    std::array<uint8_t, kMaxStoredPacket> codec_in_ { }; // packet copied out of buffer_
    std::vector<float> decode_buf_; // one decoded frame, channels_ interleaved
    std::vector<float> mono_buf_; // downmix scratch
    // Playback runs one frame behind the decoder so the similarity search has
    // context: held_ is emitted now, pending_ is the frame after it.
    std::vector<float> held_;
    std::vector<float> pending_;
    bool have_held_ = false;
    int64_t held_ts_us_ = 0;
    std::vector<float> stage_; // held_ + pending_, the search window
    std::vector<float> scratch_; // WSOLA output

    std::atomic<bool> reset_pending_ { false };

    int channels_;
    int sample_rate_;
    int64_t frame_duration_us_;

    std::atomic<float> volume_ { 1.0f };
    std::atomic<bool> muted_ { false };
    std::atomic<bool> wait_for_video_ { false };
    std::atomic<float> pan_ { 0.5f };

    uint64_t id_ = 0;

    utils::Counter packets_received_;
    utils::Counter drop_count_;
    utils::Counter conceal_count_;
    utils::Counter fec_count_;
    utils::Counter underrun_count_;
    utils::Counter decode_error_count_;
    utils::Counter stretch_count_;
    utils::Counter resync_count_;
    utils::Counter total_samples_out_;
    utils::Counter conceal_samples_;
    utils::Counter fec_samples_;
    utils::Counter silence_samples_;
    utils::Counter stretch_in_samples_;
    utils::Counter stretch_out_samples_;

    static std::atomic<uint64_t> next_id_;
};
