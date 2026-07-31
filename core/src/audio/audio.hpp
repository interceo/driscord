#pragma once

#include "opus_codec.hpp"
#include "pcm_ring.hpp"
#include "sync/media_clock.hpp"
#include "utils/expected.hpp"
#include "utils/metrics.hpp"
#include "utils/reorder_buffer.hpp"
#include "utils/spinlock.hpp"
#include "utils/vector_view.hpp"
#include "wsola.hpp"

#include <algorithm>
#include <array>
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
    uint32_t send_seq_ = 0;
    // Set while the noise gate / mute is suppressing output, so the next packet
    // emitted can be flagged as the start of a talkspurt.
    bool in_silence_ = true;
};

// Receives one audio stream from one peer and turns it back into a continuous
// mono signal for the mixer.
//
// Packets are held undecoded and ordered by sequence number, and decoded only
// when the output device asks for samples. Keeping them encoded until that
// moment is what makes concealment work: a hole in the sequence can be filled
// with Opus's own loss concealment, or — when the following packet has already
// arrived — with the in-band FEC copy it carries, which is real audio rather
// than an approximation.
//
// How long packets wait is decided by the shared MediaClock, so this stream
// lands on the same playout timeline as everything else from that peer. When
// the amount buffered drifts away from that target, the difference is taken
// out by time-stretching rather than by dropping or repeating whole frames.
class AudioReceiver {
public:
    AudioReceiver(std::shared_ptr<avsync::MediaClock> clock,
        int channels = 1,
        int sample_rate = opus::kSampleRate);
    ~AudioReceiver();

    AudioReceiver(const AudioReceiver&) = delete;
    AudioReceiver& operator=(const AudioReceiver&) = delete;

    // Network thread.
    void push_packet(utils::vector_view<const uint8_t> data);

    // Audio callback. Writes exactly `frames` mono samples, padding with
    // silence if the stream cannot supply them. Returns the number of real
    // (non-padded) samples, for diagnostics only.
    size_t read(float* out, size_t frames);

    void set_volume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }

    void set_muted(bool m) { muted_.store(m); }
    bool muted() const { return muted_.load(); }

    void set_pan(float p) { pan_.store(std::clamp(p, 0.0f, 1.0f)); }
    float pan() const { return pan_.load(); }

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
        int64_t target_delay_ms = 0;
        int64_t actual_delay_ms = 0;
        // Sender timestamp of the audio about to be played. Directly
        // comparable with a video frame's sender_ts_us from the same peer,
        // which is what makes A/V alignment measurable rather than assumed.
        int64_t playout_ts_us = 0;
    };
    Stats stats() const;

private:
    // One packet, stored whole so the network thread never allocates. Sized
    // for stereo Opus at the highest bitrate this app configures; anything
    // larger is counted and dropped rather than silently truncated.
    static constexpr size_t kMaxStoredPacket = 512;
    // ~2.5 s of reordering room at 20 ms per packet.
    static constexpr size_t kBufferCapacity = 128;
    // Room for the two frames a time-scale correction consumes plus the pitch
    // period it may insert.
    static constexpr size_t kStageCapacity = 4096;
    // Only has to absorb the mismatch between Opus frames and the device
    // period; the playout delay itself is held upstream as encoded packets.
    static constexpr size_t kRingCapacity = 16384;
    // ~0.5 s. Past this the peer has stopped sending rather than lost packets,
    // and further concealment is invention.
    static constexpr int kMaxConsecutiveConceals = 25;
    // Ceiling on saved-up time-stretch allowance, so a long quiet stretch
    // cannot be spent all at once.
    static constexpr int64_t kMaxStretchBudgetUs = 30'000;

    struct Packet {
        uint16_t len = 0;
        uint32_t flags = 0;
        int64_t sender_ts_us = 0;
        std::array<uint8_t, kMaxStoredPacket> data { };
    };

    // Produces one more span of audio into stage_. False if nothing is
    // available or the stream has not reached its playout deadline yet.
    bool playout_step();
    // Decodes (or conceals) the packet at the cursor into `dst`, advancing
    // the read cursor by exactly one frame.
    bool decode_into(std::vector<float>& dst);
    // True when the packet at the read cursor has actually arrived, as opposed
    // to being missing and about to be concealed.
    bool next_packet_ready() const;
    void refill_stretch_budget(int64_t now);

    std::shared_ptr<avsync::MediaClock> clock_;

    // Guards buffer_ between the network thread and the audio callback. The
    // critical section is a memcpy of at most kMaxStoredPacket bytes plus a
    // few bitmask operations, so spinning is cheaper than a futex round trip —
    // and unlike the previous try-lock, a contended callback no longer
    // silently emits a hole in the audio.
    mutable utils::SpinLock buffer_lock_;
    utils::ReorderBuffer<Packet, kBufferCapacity> buffer_;

    OpusDecode decoder_;
    Wsola wsola_;
    PcmRing ring_;

    // Audio-callback state.
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
    std::vector<float> stage_; // held_ + pending_, the search window
    std::vector<float> scratch_; // WSOLA output

    std::atomic<bool> reset_pending_ { false };

    int channels_;
    int sample_rate_;
    int64_t frame_duration_us_;

    std::atomic<float> volume_ { 1.0f };
    std::atomic<bool> muted_ { false };
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

    static std::atomic<uint64_t> next_id_;
};
