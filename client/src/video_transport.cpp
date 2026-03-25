#include "video_transport.hpp"

#include "log.hpp"

#include <algorithm>
#include <cstring>

namespace {

constexpr uint8_t kKeyframeRequestTag = 0x01;
constexpr uint8_t kStopStreamTag      = 0x02;

} // namespace

VideoTransport::VideoTransport(Transport& transport)
    : transport_(transport) {
    transport.register_channel({
        .label           = "video",
        .unordered       = true,
        .max_retransmits = 0,
        .on_data =
            [this](const std::string& peer_id, const uint8_t* data, size_t len) {
                if (len == 1) {
                    if (data[0] == kKeyframeRequestTag) {
                        std::scoped_lock lk(sink_mutex_);
                        if (on_keyframe_needed_) {
                            on_keyframe_needed_();
                        }
                        return;
                    }
                    if (data[0] == kStopStreamTag) {
                        remove_streaming_peer(peer_id);
                        return;
                    }
                }
                on_chunk(peer_id, data, len);
            },
        .on_open =
            [this](const std::string& peer_id) {
                // Reset stale streaming state from any previous connection to this peer.
                // remove_streaming_peer is a no-op if the peer wasn't streaming before.
                remove_streaming_peer(peer_id);
                std::scoped_lock lk(sink_mutex_);
                if (on_keyframe_needed_) {
                    on_keyframe_needed_();
                }
            },
        .on_close = [this](const std::string& peer_id) { remove_streaming_peer(peer_id); },
    });
}

void VideoTransport::send_video(const uint8_t* data, size_t len) {
    if (len == 0) {
        return;
    }

    const auto
        total_chunks = static_cast<uint16_t>((len + kChunkPayloadSize - 1) / kChunkPayloadSize);
    const uint64_t frame_id = next_frame_id_++;

    chunk_buf_.resize(protocol::ChunkHeader::kWireSize + kChunkPayloadSize);

    for (uint16_t i = 0; i < total_chunks; ++i) {
        const size_t offset    = static_cast<size_t>(i) * kChunkPayloadSize;
        const size_t chunk_len = std::min(kChunkPayloadSize, len - offset);

        protocol::ChunkHeader ch{
            .frame_id     = frame_id,
            .chunk_idx    = i,
            .total_chunks = total_chunks,
        };
        ch.serialize(chunk_buf_.data());
        std::memcpy(chunk_buf_.data() + protocol::ChunkHeader::kWireSize, data + offset, chunk_len);

        transport_.send_on_channel(
            "video",
            chunk_buf_.data(),
            protocol::ChunkHeader::kWireSize + chunk_len
        );
    }
}

void VideoTransport::send_keyframe_request() {
    transport_.send_on_channel("video", &kKeyframeRequestTag, 1);
}

void VideoTransport::send_stop_stream() {
    transport_.send_on_channel("video", &kStopStreamTag, 1);
}

void VideoTransport::on_new_streaming_peer(std::function<void(const std::string&)> cb) {
    std::scoped_lock lk(streaming_mutex_);
    on_new_streaming_peer_ = std::move(cb);
}

void VideoTransport::on_streaming_peer_removed(std::function<void(const std::string&)> cb) {
    std::scoped_lock lk(streaming_mutex_);
    on_streaming_peer_removed_ = std::move(cb);
}

void VideoTransport::remove_streaming_peer(const std::string& peer_id) {
    bool was_present;
    std::function<void(const std::string&)> cb;
    {
        std::scoped_lock lk(streaming_mutex_);
        was_present = seen_streaming_.erase(peer_id) > 0;
        cb          = on_streaming_peer_removed_;
    }
    if (was_present && cb) {
        cb(peer_id);
    }
}

void VideoTransport::add_watched_peer(const std::string& peer_id) {
    std::scoped_lock lk(sink_mutex_);
    watched_peers_.insert(peer_id);
}

void VideoTransport::remove_watched_peer(const std::string& peer_id) {
    std::scoped_lock lk(sink_mutex_);
    watched_peers_.erase(peer_id);
}

void VideoTransport::clear_watched_peers() {
    std::scoped_lock lk(sink_mutex_);
    watched_peers_.clear();
}

bool VideoTransport::watching() const {
    std::scoped_lock lk(sink_mutex_);
    return !watched_peers_.empty();
}

void VideoTransport::set_video_sink(VideoPacketCb video_cb, KeyframeCb kf_cb) {
    std::scoped_lock lk(sink_mutex_);
    on_video_sink_      = std::move(video_cb);
    on_keyframe_needed_ = std::move(kf_cb);
}

void VideoTransport::clear_video_sink() {
    std::scoped_lock lk(sink_mutex_);
    on_video_sink_      = nullptr;
    on_keyframe_needed_ = nullptr;
}

void VideoTransport::on_assembled(const std::string& peer_id, const uint8_t* data, size_t len) {
    {
        std::function<void(const std::string&)> cb;
        bool is_new;
        {
            std::scoped_lock lk(streaming_mutex_);
            is_new = seen_streaming_.insert(peer_id).second;
            cb     = on_new_streaming_peer_;
        }
        if (is_new && cb) {
            cb(peer_id);
        }
    }
    {
        std::scoped_lock lk(sink_mutex_);
        if (on_video_sink_ && watched_peers_.count(peer_id) > 0) {
            on_video_sink_(peer_id, data, len);
        }
    }
}

void VideoTransport::on_chunk(const std::string& peer_id, const uint8_t* data, size_t len) {
    if (len <= protocol::ChunkHeader::kWireSize) {
        return;
    }

    const auto ch = protocol::ChunkHeader::deserialize(data);
    if (ch.total_chunks == 0 || ch.chunk_idx >= ch.total_chunks) {
        return;
    }
    if (ch.total_chunks > kMaxChunksPerFrame) {
        return;
    }

    const uint8_t* payload   = data + protocol::ChunkHeader::kWireSize;
    const size_t payload_len = len - protocol::ChunkHeader::kWireSize;

    auto& peer_map = peer_assembly_[peer_id];
    auto& fa       = peer_map[ch.frame_id];
    if (fa.total == 0) {
        for (auto it = peer_map.begin(); it != peer_map.end();) {
            if (it->first + kMaxAssemblyFrames < ch.frame_id) {
                it = peer_map.erase(it);
            } else {
                ++it;
            }
        }
        fa.total = ch.total_chunks;
        fa.buffer.resize(static_cast<size_t>(ch.total_chunks) * kChunkPayloadSize);
        fa.received.assign(ch.total_chunks, false);
    }

    if (fa.total != ch.total_chunks) {
        return;
    }

    if (!fa.received[ch.chunk_idx]) {
        std::memcpy(
            fa.buffer.data() + static_cast<size_t>(ch.chunk_idx) * kChunkPayloadSize,
            payload,
            payload_len
        );
        fa.received[ch.chunk_idx] = true;
        ++fa.received_count;
        fa.actual_size = std::max(
            fa.actual_size,
            static_cast<size_t>(ch.chunk_idx) * kChunkPayloadSize + payload_len
        );
    }

    if (fa.received_count < fa.total) {
        return;
    }

    fa.buffer.resize(fa.actual_size);
    on_assembled(peer_id, fa.buffer.data(), fa.actual_size);
    peer_map.erase(ch.frame_id);
}
