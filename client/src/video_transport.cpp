#include "video_transport.hpp"

#include "log.hpp"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint8_t kKeyframeRequestTag = 0x01;
} // namespace

VideoTransport::VideoTransport(Transport& transport) : transport_(transport) {
    transport.register_channel({
        .label           = "video",
        .unordered       = true,
        .max_retransmits = -1, // reliable + unordered: no HOL blocking between messages
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

    const auto total_chunks = static_cast<uint16_t>((len + kChunkPayloadSize - 1) / kChunkPayloadSize);
    const uint16_t frame_id = next_frame_id_++;

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

    std::vector<uint8_t> assembled;

    {
        std::scoped_lock lk(assembly_mutex_);

        auto& fa = assembly_[ch.frame_id];

        // First chunk seen for this frame_id — initialise slot.
        if (fa.total == 0) {
            // Evict oldest incomplete frames to keep memory bounded.
            if (assembly_.size() > kMaxAssemblyFrames) {
                assembly_.erase(assembly_.begin());
            }
            fa.total  = ch.total_chunks;
            fa.chunks.resize(ch.total_chunks);
        }

        // Guard against inconsistent total_chunks across retransmissions.
        if (fa.total != ch.total_chunks) {
            return;
        }

        // Store chunk only once (idempotent on retransmit).
        if (fa.chunks[ch.chunk_idx].empty()) {
            fa.chunks[ch.chunk_idx].assign(payload, payload + payload_len);
            ++fa.received;
        }

        if (fa.received < fa.total) {
            return; // still waiting for more chunks
        }

        // All chunks received — build contiguous frame buffer.
        size_t total_size = 0;
        for (const auto& c : fa.chunks) {
            total_size += c.size();
        }
        assembled.reserve(total_size);
        for (const auto& c : fa.chunks) {
            assembled.insert(assembled.end(), c.begin(), c.end());
        }
        assembly_.erase(ch.frame_id);
    }

    if (on_video_ && !assembled.empty()) {
        on_video_(peer_id, assembled.data(), assembled.size());
    }
}
