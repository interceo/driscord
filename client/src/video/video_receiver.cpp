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

    pending_ = PendingFrame{
        std::vector<uint8_t>(
            reassembler_.buf.data() + protocol::VideoHeader::kWireSize,
            reassembler_.buf.data() + reassembler_.buf.size()
        ),
        vh.bitrate_kbps,
        vh.sender_ts,
        vh.frame_duration_us,
    };
}

const VideoJitter::Frame* VideoReceiver::update() {
    std::optional<PendingFrame> frame;
    {
        std::scoped_lock lk(mutex_);
        frame = std::move(pending_);
        pending_.reset();
    }

    if (frame && decoder_.ready()) {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (decoder_.decode(frame->data.data(), frame->data.size(), rgba, w, h)) {
            decode_failures_ = 0;
            measured_kbps_ = static_cast<int>(frame->kbps);
            video_.push(std::move(rgba), w, h, frame->duration_us, frame->ts);
        } else {
            ++decode_failures_;
            const auto now = utils::Now();
            if (on_keyframe_needed_ && (decode_failures_ == 1 || utils::ElapsedMs(last_keyframe_req_, now) >= 500)) {
                on_keyframe_needed_();
                last_keyframe_req_ = now;
            }
        }
    }

    return video_.pop();
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
        pending_.reset();
    }
    video_.reset();
    decode_failures_ = 0;
    measured_kbps_ = 0;
}
