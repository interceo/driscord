#pragma once

#include "audio/capture/system_audio_capture.hpp"
#include "utils/opus_codec.hpp"
#include "video/capture/screen_capture.hpp"
#include "video/video_sender.hpp"

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
