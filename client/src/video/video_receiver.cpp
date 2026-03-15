#include "video_receiver.hpp"

#include "log.hpp"
#include "utils/byte_utils.hpp"

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
    if (len <= protocol::ChunkHeader::kWireSize) {
        return;
    }

    const auto ch = protocol::ChunkHeader::deserialize(data);

    if (ch.total_chunks == 0 || ch.chunk_idx >= ch.total_chunks) {
        return;
    }

    const uint8_t* chunk_data = data + protocol::ChunkHeader::kWireSize;
    const size_t chunk_len = len - protocol::ChunkHeader::kWireSize;

    std::vector<uint8_t> frame_data;
    uint32_t kbps = 0;
    utils::WallTimestamp sender_ts{};

    {
        std::scoped_lock lk(mutex_);

        current_peer_ = peer_id;
        last_packet_ = utils::Now();

        if (ch.frame_id != reassembler_.frame_id || ch.total_chunks != reassembler_.total) {
            if (reassembler_.total > 0 && reassembler_.got < reassembler_.total && on_keyframe_needed_) {
                on_keyframe_needed_();
            }
            reassembler_ = ChunkReassembler{};
            reassembler_.frame_id = ch.frame_id;
            reassembler_.total = ch.total_chunks;
        }

        const size_t offset = static_cast<size_t>(ch.chunk_idx) * protocol::kMaxChunkPayload;
        size_t needed = offset + chunk_len;
        if (reassembler_.buf.size() < needed) {
            reassembler_.buf.resize(needed);
        }
        std::memcpy(reassembler_.buf.data() + offset, chunk_data, chunk_len);
        ++reassembler_.got;

        if (reassembler_.got < ch.total_chunks) {
            return;
        }

        if (reassembler_.buf.size() <= protocol::VideoHeader::kWireSize) {
            return;
        }

        const auto vh = protocol::VideoHeader::deserialize(reassembler_.buf.data());

        if (vh.width == 0 || vh.height == 0 || vh.width > 7680 || vh.height > 4320) {
            return;
        }

        frame_data.assign(
            reassembler_.buf.data() + protocol::VideoHeader::kWireSize,
            reassembler_.buf.data() + reassembler_.buf.size()
        );
        kbps = vh.bitrate_kbps;
        sender_ts = vh.sender_ts;
    }

    if (!decoder_.ready()) {
        return;
    }

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (decoder_.decode(frame_data.data(), frame_data.size(), rgba, w, h)) {
        decode_failures_ = 0;
        measured_kbps_.store(static_cast<int>(kbps), std::memory_order_relaxed);
        ++push_count_;
        if (push_count_ % 30 == 0) {
            LOG_INFO()
                << "[video-recv] push#" << push_count_ << " sender_ts=" << utils::WallToMs(sender_ts)
                << " age_ms=" << utils::WallElapsedMs(sender_ts) << " queue=" << video_.queue_size();
        }
        video_.push(std::move(rgba), w, h, sender_ts);
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
        reassembler_ = ChunkReassembler{};
    }
    video_.reset();
    decode_failures_ = 0;
    measured_kbps_.store(0, std::memory_order_relaxed);
}
