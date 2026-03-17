#pragma once

#include "audio/audio_receiver.hpp"
#include "audio/capture/system_audio_capture.hpp"
#include "utils/opus_codec.hpp"
#include "utils/protocol.hpp"
#include "utils/time.hpp"
#include "video/capture/screen_capture.hpp"
#include "video/video_receiver.hpp"
#include "video/video_sender.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ScreenSession {
public:
    using SendCb = std::function<void(const uint8_t* data, size_t len)>;

    ScreenSession(int buffer_ms, int max_sync_gap_ms)
        : video_recv_(buffer_ms, max_sync_gap_ms),
          audio_recv_(std::make_shared<AudioReceiver>(buffer_ms, /*channels=*/2)) {}

    ~ScreenSession();

    // -------------------------------------------------------------------------
    // Sender side
    // -------------------------------------------------------------------------

    bool start_sharing(
        const CaptureTarget& target,
        int max_w,
        int max_h,
        int fps,
        int bitrate_kbps,
        bool share_audio,
        SendCb on_video,
        SendCb on_screen_audio
    );

    void stop_sharing();

    bool sharing() const { return video_sender_.sharing(); }
    bool sharing_audio() const { return system_audio_capture_ && system_audio_capture_->running(); }

    void force_keyframe() { video_sender_.force_keyframe(); }
    int sender_kbps() const { return video_sender_.measured_kbps(); }

    // -------------------------------------------------------------------------
    // Receiver side
    // -------------------------------------------------------------------------

    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len) {
        video_recv_.push_video_packet(peer_id, data, len);
    }

    void push_audio_packet(const uint8_t* data, size_t len) { audio_recv_->push_packet(data, len); }

    const VideoJitter::Frame* update() { return video_recv_.update(); }

    std::shared_ptr<AudioReceiver> audio_receiver() { return audio_recv_; }
    std::shared_ptr<const AudioReceiver> audio_receiver() const { return audio_recv_; }

    void set_keyframe_callback(std::function<void()> fn) { video_recv_.set_keyframe_callback(std::move(fn)); }

    std::string active_peer() const { return video_recv_.active_peer(); }
    bool active() const { return video_recv_.active(); }
    int measured_kbps() const { return video_recv_.measured_kbps(); }

    VideoJitter::Stats video_stats() const { return video_recv_.video_stats(); }
    AudioReceiver::Stats audio_stats() const { return audio_recv_->stats(); }

    void reset();

private:
    void on_audio_captured_(const float* samples, size_t frames, int channels);

    // sender
    VideoSender video_sender_;
    std::unique_ptr<ScreenCapture> screen_capture_;
    std::unique_ptr<SystemAudioCapture> system_audio_capture_;
    std::unique_ptr<OpusEncode> screen_audio_encoder_;
    SendCb on_screen_audio_;
    std::vector<float> screen_audio_buf_;
    std::vector<uint8_t> screen_audio_encode_buf_;
    size_t screen_audio_pos_ = 0;
    uint64_t screen_audio_seq_ = 0;

    // receiver
    VideoReceiver video_recv_;
    std::shared_ptr<AudioReceiver> audio_recv_;
};
