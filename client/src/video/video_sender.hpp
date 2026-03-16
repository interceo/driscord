#pragma once

#include "utils/protocol.hpp"
#include "utils/video_codec.hpp"
#include "capture/screen_capture.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class VideoSender {
public:
    using SendCb = std::function<void(const uint8_t* data, size_t len)>;

    VideoSender();
    ~VideoSender();

    VideoSender(const VideoSender&) = delete;
    VideoSender& operator=(const VideoSender&) = delete;

    bool start(int fps, int base_bitrate_kbps, SendCb on_video);
    void stop();

    // Called by external screen capture to deliver frames.
    void push_frame(ScreenCapture::Frame frame);

    bool sharing() const { return sharing_; }
    void force_keyframe() { video_encoder_.force_keyframe(); }
    int measured_kbps() const { return video_encoder_.measured_kbps(); }
    int width() const { return video_encoder_.width(); }
    int height() const { return video_encoder_.height(); }

private:
    void encode_loop();

    std::atomic<bool> sharing_{false};

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

    SendCb on_video_;
};
