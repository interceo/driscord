#include "video_receiver.hpp"

#include "log.hpp"
#include "utils/time.hpp"

#include <cstring>

using namespace utils;

namespace {
constexpr int kStaleSeconds = 3;
}  // namespace

VideoReceiver::VideoReceiver(int buffer_ms, int /*max_sync_gap_ms*/) : video_(buffer_ms) {
    if (!decoder_.init()) {
        LOG_ERROR() << "failed to init video decoder";
    }
}

VideoReceiver::~VideoReceiver() = default;

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

    const uint8_t* encoded = data + protocol::VideoHeader::kWireSize;
    const size_t encoded_len = len - protocol::VideoHeader::kWireSize;

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    // Measure actual receive throughput (regardless of decode success).
    bytes_since_calc_ += len;
    {
        auto now = utils::Now();
        auto elapsed_ms = utils::ElapsedMs(last_calc_, now);
        if (elapsed_ms >= 1000) {
            measured_kbps_.store(static_cast<int>(bytes_since_calc_ * 8 / elapsed_ms),
                                 std::memory_order_relaxed);
            bytes_since_calc_ = 0;
            last_calc_ = now;
        }
    }

    if (decoder_.decode(encoded, encoded_len, rgba, w, h)) {
        decode_failures_ = 0;
        ++push_count_;
        if (push_count_ % 30 == 0) {
            LOG_INFO()
                << "[video-recv] push#" << push_count_ << " sender_ts=" << utils::WallToMs(vh.sender_ts)
                << " age_ms=" << utils::WallElapsedMs(vh.sender_ts) << " queue=" << video_.queue_size();
        }
        video_.push(std::move(rgba), w, h, vh.sender_ts);
    } else {
        ++decode_failures_;
        const auto now = utils::Now();
        if (on_keyframe_needed_ && (decode_failures_ == 1 || utils::ElapsedMs(last_keyframe_req_, now) >= 500)) {
            on_keyframe_needed_();
            last_keyframe_req_ = now;
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

const VideoJitter::Frame* VideoReceiver::current_frame() const {
    return video_.current();
}

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
    video_.reset();
    decode_failures_ = 0;
    measured_kbps_.store(0, std::memory_order_relaxed);
    bytes_since_calc_ = 0;
    last_calc_ = utils::Now();
}
