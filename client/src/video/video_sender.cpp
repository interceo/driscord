#include "video_sender.hpp"

#include "log.hpp"
#include "utils/byte_utils.hpp"

#include <algorithm>
#include <cstring>

using namespace utils;

VideoSender::VideoSender() = default;
VideoSender::~VideoSender() { stop(); }

bool VideoSender::start(int fps, int base_bitrate_kbps, SendCb on_video) {
    if (sharing_) {
        return false;
    }

    fps_ = fps;
    base_bitrate_kbps_ = base_bitrate_kbps;
    on_video_ = std::move(on_video);

    encode_running_ = true;
    encode_thread_ = std::thread(&VideoSender::encode_loop, this);
    sharing_ = true;

    LOG_INFO() << "video sender started fps=" << fps << " bitrate=" << base_bitrate_kbps << " kbps";
    return true;
}

void VideoSender::stop() {
    if (!sharing_) {
        return;
    }

    encode_running_ = false;
    frame_cv_.notify_one();
    if (encode_thread_.joinable()) {
        encode_thread_.join();
    }

    video_encoder_.shutdown();
    sharing_ = false;
    on_video_ = nullptr;

    LOG_INFO() << "video sender stopped";
}

void VideoSender::push_frame(ScreenCapture::Frame frame) {
    if (!sharing_) {
        return;
    }
    frame.capture_ts = std::chrono::system_clock::now();
    {
        std::scoped_lock lk(frame_mutex_);
        pending_frame_ = std::move(frame);
        frame_ready_ = true;
    }
    frame_cv_.notify_one();
}

void VideoSender::encode_loop() {
    while (encode_running_) {
        ScreenCapture::Frame frame;
        {
            std::unique_lock lk(frame_mutex_);
            frame_cv_.wait_for(lk, std::chrono::milliseconds(100), [this] { return frame_ready_ || !encode_running_; });
            if (!frame_ready_) {
                continue;
            }
            frame = std::move(pending_frame_);
            frame_ready_ = false;
        }

        if (frame.data.empty() || !encode_running_) {
            continue;
        }

        if (frame.width != video_encoder_.width() || frame.height != video_encoder_.height()) {
            if (!video_encoder_.init(frame.width, frame.height, fps_, base_bitrate_kbps_)) {
                continue;
            }
        }

        const auto& encoded = video_encoder_.encode(frame.data.data(), frame.width, frame.height);
        if (encoded.empty()) {
            continue;
        }

        const auto capture_ts = frame.capture_ts.time_since_epoch().count() != 0 ? frame.capture_ts : WallNow();

        const protocol::VideoHeader vh{
            .width = static_cast<uint32_t>(frame.width),
            .height = static_cast<uint32_t>(frame.height),
            .sender_ts = capture_ts,
            .bitrate_kbps = static_cast<uint32_t>(video_encoder_.measured_kbps()),
            .frame_duration_us = static_cast<uint32_t>(1'000'000 / fps_),
        };
        frame_buf_.resize(protocol::VideoHeader::kWireSize + encoded.size());
        vh.serialize(frame_buf_.data());
        std::memcpy(frame_buf_.data() + protocol::VideoHeader::kWireSize, encoded.data(), encoded.size());

        on_video_(frame_buf_.data(), frame_buf_.size());
    }
}
