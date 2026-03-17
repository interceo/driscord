#pragma once

#include "audio/audio.hpp"
#include "audio/capture/system_audio_capture.hpp"
#include "utils/opus_codec.hpp"
#include "video/capture/screen_capture.hpp"
#include "video/video.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Sender half of a screen-share session: captures screen + optional system
// audio, encodes, and fires callbacks for the transport to send.
class ScreenSender {
public:
    using SendCb = std::function<void(const uint8_t* data, size_t len)>;

    ScreenSender() = default;
    ~ScreenSender();

    ScreenSender(const ScreenSender&) = delete;
    ScreenSender& operator=(const ScreenSender&) = delete;

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

private:
    void on_audio_captured_(const float* samples, size_t frames, int channels);

    VideoSender video_sender_;
    std::unique_ptr<ScreenCapture> screen_capture_;
    std::unique_ptr<SystemAudioCapture> system_audio_capture_;
    std::unique_ptr<OpusEncode> screen_audio_encoder_;
    SendCb on_screen_audio_;
    std::vector<float> screen_audio_buf_;
    std::vector<uint8_t> screen_audio_encode_buf_;
    size_t screen_audio_pos_ = 0;
    uint64_t screen_audio_seq_ = 0;
};

// Receiver half of a screen-share session: reassembles video + audio and
// presents them in sync to the caller.
//
// A/V sync strategy (video-side):
//   - If the next video frame is more than kHoldThresholdMs ahead of the last
//     audio frame delivered to the mixer, freeze video until audio catches up.
//   - If audio is lagging more than kDrainThresholdMs behind video, stale
//     audio frames are discarded so catch-up happens quickly.
class ScreenReceiver {
public:
    ScreenReceiver(int buffer_ms, int max_sync_gap_ms);
    ~ScreenReceiver() = default;

    ScreenReceiver(const ScreenReceiver&) = delete;
    ScreenReceiver& operator=(const ScreenReceiver&) = delete;

    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len);
    void push_audio_packet(const uint8_t* data, size_t len);

    // Advance playback by one video frame. Returns nullptr when nothing is ready.
    // Implements A/V sync: may hold the current frame if video is ahead of audio.
    const VideoJitter::Frame* update();

    std::shared_ptr<AudioReceiver> audio_receiver() { return audio_recv_; }
    std::shared_ptr<const AudioReceiver> audio_receiver() const { return audio_recv_; }

    void set_keyframe_callback(std::function<void()> fn);

    std::string active_peer() const;
    bool active() const;
    int measured_kbps() const;

    VideoJitter::Stats video_stats() const;
    AudioReceiver::Stats audio_stats() const;

    void reset();

private:
    VideoReceiver video_recv_;
    std::shared_ptr<AudioReceiver> audio_recv_;
};
