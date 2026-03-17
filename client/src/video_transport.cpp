#include "video_transport.hpp"

#include "log.hpp"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint8_t kKeyframeRequestTag = 0x01;
}  // namespace

VideoTransport::VideoTransport(Transport& transport) : transport_(transport) {
    transport.register_channel({
        .label           = "video",
        .unordered       = true,
        .max_retransmits = 0,  // unreliable: lost chunks drop the frame, decoder recovers at next IDR
        .on_data = [this](const std::string& peer_id, const uint8_t* data, size_t len) {
            if (len == 1 && data[0] == kKeyframeRequestTag) {
                if (on_kf_req_) {
                    on_kf_req_();
                }
                return;
            }
            on_chunk(peer_id, data, len);
        },
        .on_open = [this](const std::string& /*peer_id*/) {
            if (on_opened_) {
                on_opened_();
            }
        },
    });
}

void VideoTransport::send_video(const uint8_t* data, size_t len) {
    if (len == 0) {
        return;
    }

    const auto     total_chunks = static_cast<uint16_t>((len + kChunkPayloadSize - 1) / kChunkPayloadSize);
    const uint64_t frame_id     = next_frame_id_++;

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

        transport_.send_on_channel("video", chunk_buf_.data(),
                                   protocol::ChunkHeader::kWireSize + chunk_len);
    }
}

void VideoTransport::send_keyframe_request() {
    transport_.send_on_channel("video", &kKeyframeRequestTag, 1);
}

void VideoTransport::on_chunk(const std::string& peer_id, const uint8_t* data, size_t len) {
    if (len <= protocol::ChunkHeader::kWireSize) {
        return;
    }

    const auto ch = protocol::ChunkHeader::deserialize(data);
    if (ch.total_chunks == 0 || ch.chunk_idx >= ch.total_chunks) {
        return;
    }

    const uint8_t* payload     = data + protocol::ChunkHeader::kWireSize;
    const size_t   payload_len = len  - protocol::ChunkHeader::kWireSize;

    auto& fa = assembly_[ch.frame_id];
    if (fa.total == 0) {
        for (auto it = assembly_.begin(); it != assembly_.end(); ) {
            if (it->first + kMaxAssemblyFrames < ch.frame_id) {
                it = assembly_.erase(it);
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
        std::memcpy(fa.buffer.data() + static_cast<size_t>(ch.chunk_idx) * kChunkPayloadSize,
                    payload, payload_len);
        fa.received[ch.chunk_idx] = true;
        ++fa.received_count;
        fa.actual_size = std::max(fa.actual_size,
            static_cast<size_t>(ch.chunk_idx) * kChunkPayloadSize + payload_len);
    }

    if (fa.received_count < fa.total) {
        return;
    }

    fa.buffer.resize(fa.actual_size);
    if (on_video_) {
        on_video_(peer_id, fa.buffer.data(), fa.actual_size);
    }
    assembly_.erase(ch.frame_id);
}
