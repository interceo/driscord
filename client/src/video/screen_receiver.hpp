#pragma once

#include "stream_jitter.hpp"
#include "utils/opus_codec.hpp"
#include "utils/protocol.hpp"
#include "utils/video_codec.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class ScreenReceiver {
public:
    ScreenReceiver(int buffer_ms, int max_sync_gap_ms);
    ~ScreenReceiver();

    ScreenReceiver(const ScreenReceiver&) = delete;
    ScreenReceiver& operator=(const ScreenReceiver&) = delete;

    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len);
    void push_audio_packet(const uint8_t* data, size_t len);

    const ScreenStreamJitter::VideoFrame* update();

    void set_keyframe_callback(std::function<void()> fn);

    ScreenStreamJitter* jitter() { return &jitter_; }

    std::string active_peer() const;
    bool active() const;

    void set_volume(float v) { jitter_.set_volume(v); }
    float volume() const { return jitter_.volume(); }

    int measured_kbps() const { return measured_kbps_; }

    void reset();

private:
    static constexpr int kScreenAudioChannels = 2;

    struct ChunkReassembler {
        uint16_t frame_id = 0;
        uint16_t total = 0;
        uint16_t got = 0;
        std::vector<uint8_t> buf;
    };

    struct PendingFrame {
        std::vector<uint8_t> data;
        uint32_t kbps = 0;
        utils::WallTimestamp ts{};
    };

    ScreenStreamJitter jitter_;

    mutable std::mutex mutex_;
    std::string current_peer_;
    utils::Timestamp last_packet_{};
    ChunkReassembler reassembler_;
    std::optional<PendingFrame> pending_;

    VideoDecoder decoder_;
    int decode_failures_ = 0;
    utils::Timestamp last_keyframe_req_{};
    int measured_kbps_ = 0;

    std::unique_ptr<OpusDecode> opus_decoder_;
    std::vector<float> audio_decode_buf_;
    std::vector<float> audio_mono_buf_;

    std::function<void()> on_keyframe_needed_;
};
