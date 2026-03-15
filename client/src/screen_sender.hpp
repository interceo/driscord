#pragma once

#include "utils/opus_codec.hpp"
#include "utils/protocol.hpp"
#include "utils/video_codec.hpp"
#include "video/screen_capture.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class SystemAudioCapture;

class ScreenSender {
public:
    using SendCb = std::function<void(const uint8_t* data, size_t len)>;

    ScreenSender();
    ~ScreenSender();

    ScreenSender(const ScreenSender&) = delete;
    ScreenSender& operator=(const ScreenSender&) = delete;

    bool start(
        const CaptureTarget& target,
        int max_w,
        int max_h,
        int fps,
        int base_bitrate_kbps,
        bool share_audio,
        SendCb on_video,
        SendCb on_audio
    );
    void stop();

    bool sharing() const { return sharing_; }
    bool sharing_audio() const { return sharing_audio_; }
    void force_keyframe() { video_encoder_.force_keyframe(); }
    int measured_kbps() const { return video_encoder_.measured_kbps(); }
    int width() const { return video_encoder_.width(); }
    int height() const { return video_encoder_.height(); }

private:
    void encode_loop();
    void on_audio_captured(const float* samples, size_t frames, int channels);

    static constexpr int kScreenAudioChannels = 2;

    std::atomic<bool> sharing_{false};
    std::atomic<bool> sharing_audio_{false};

    std::unique_ptr<ScreenCapture> screen_capture_;
    VideoEncoder video_encoder_;

    std::thread encode_thread_;
    std::atomic<bool> encode_running_{false};
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    ScreenCapture::Frame pending_frame_;
    bool frame_ready_ = false;

    int fps_ = 30;
    int base_bitrate_kbps_ = 4000;

    std::vector<uint8_t> frame_buf_;
    std::vector<uint8_t> send_buf_;
    uint16_t send_frame_id_ = 0;

    std::unique_ptr<SystemAudioCapture> system_audio_capture_;
    std::unique_ptr<OpusEncode> opus_encoder_;
    std::vector<float> audio_capture_buf_;
    size_t audio_capture_pos_ = 0;
    std::vector<uint8_t> audio_encode_buf_;
    uint64_t audio_send_seq_ = 0;

    SendCb on_video_;
    SendCb on_audio_;
};
