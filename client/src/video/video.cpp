#include "video.hpp"

#include "log.hpp"

#include <cstring>

namespace {

using namespace utils;

constexpr int kStaleSeconds = 3;

}  // namespace

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

VideoReceiver::VideoReceiver(int buffer_ms, int /*max_sync_gap_ms*/) : video_(buffer_ms) {
    if (!decoder_.init()) {
        LOG_ERROR() << "failed to init video decoder";
    }
    decode_running_ = true;
    decode_thread_ = std::thread(&VideoReceiver::decode_loop, this);
}

VideoReceiver::~VideoReceiver() {
    decode_running_ = false;
    decode_cv_.notify_all();
    if (decode_thread_.joinable()) {
        decode_thread_.join();
    }
}

void VideoReceiver::set_keyframe_callback(std::function<void()> fn) { on_keyframe_needed_ = std::move(fn); }

void VideoReceiver::push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len) {
    if (len <= protocol::VideoHeader::kWireSize) {
        return;
    }

    const auto vh = protocol::VideoHeader::deserialize(data);
    if (vh.width == 0 || vh.height == 0 || vh.width > 7680 || vh.height > 4320) {
        return;
    }

    {
        std::scoped_lock lk(mutex_);
        current_peer_ = peer_id;
        last_packet_ = utils::Now();
    }

    if (!decoder_.ready()) {
        return;
    }

    bytes_since_calc_ += len;
    {
        auto now = utils::Now();
        auto elapsed_ms = utils::ElapsedMs(last_calc_, now);
        if (elapsed_ms >= 1000) {
            measured_kbps_.store(static_cast<int>(bytes_since_calc_ * 8 / elapsed_ms), std::memory_order_relaxed);
            bytes_since_calc_ = 0;
            last_calc_ = now;
        }
    }

    const uint8_t* encoded = data + protocol::VideoHeader::kWireSize;
    const size_t encoded_len = len - protocol::VideoHeader::kWireSize;

    {
        std::unique_lock lk(decode_queue_mutex_);
        if (decode_queue_.size() >= kMaxDecodeQueue) {
            decode_queue_.pop_front();
            LOG_WARNING() << "[video-recv] decode queue full, dropping frame";
        }
        DecodeJob job;
        job.encoded.assign(encoded, encoded + encoded_len);
        job.vh = vh;
        decode_queue_.push_back(std::move(job));
    }
    decode_cv_.notify_one();
}

void VideoReceiver::decode_loop() {
    while (true) {
        DecodeJob job;
        {
            std::unique_lock lk(decode_queue_mutex_);
            decode_cv_.wait(lk, [this] { return !decode_queue_.empty() || !decode_running_; });
            if (!decode_running_ && decode_queue_.empty()) {
                break;
            }
            job = std::move(decode_queue_.front());
            decode_queue_.pop_front();
        }

        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (decoder_.decode(job.encoded.data(), job.encoded.size(), rgba, w, h)) {
            decode_failures_ = 0;
            ++push_count_;
            if (push_count_ % 30 == 0) {
                LOG_INFO()
                    << "[video-recv] push#" << push_count_ << " sender_ts=" << utils::WallToMs(job.vh.sender_ts)
                    << " age_ms=" << utils::WallElapsedMs(job.vh.sender_ts) << " queue=" << video_.queue_size();
            }
            video_.push(std::move(rgba), w, h, job.vh.sender_ts);
        } else {
            ++decode_failures_;
            const auto now = utils::Now();
            if (on_keyframe_needed_ && (decode_failures_ == 1 || utils::ElapsedMs(last_keyframe_req_, now) >= 500)) {
                on_keyframe_needed_();
                last_keyframe_req_ = now;
            }
        }
    }
}

const VideoJitter::Frame* VideoReceiver::update() {
    auto* result = video_.pop();
    ++pop_count_;
    if (pop_count_ % 60 == 0) {
        LOG_INFO()
            << "[video-recv] pop#" << pop_count_ << " got=" << (result ? "frame" : "null")
            << " queue=" << video_.queue_size();
    }
    return result;
}

const VideoJitter::Frame* VideoReceiver::current_frame() const { return video_.current(); }

std::string VideoReceiver::active_peer() const {
    std::scoped_lock lk(mutex_);
    return current_peer_;
}

bool VideoReceiver::active() const {
    std::scoped_lock lk(mutex_);
    if (current_peer_.empty()) {
        return false;
    }
    return utils::ElapsedMs(last_packet_) / 1000 <= kStaleSeconds;
}

void VideoReceiver::reset() {
    {
        std::scoped_lock lk(mutex_);
        current_peer_.clear();
    }
    {
        std::scoped_lock lk(decode_queue_mutex_);
        decode_queue_.clear();
    }
    video_.reset();
    decode_failures_ = 0;
    measured_kbps_.store(0, std::memory_order_relaxed);
    bytes_since_calc_ = 0;
    last_calc_ = utils::Now();
}
