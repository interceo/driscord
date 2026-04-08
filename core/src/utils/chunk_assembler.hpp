#pragma once

#include "protocol.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

namespace utils {

template <typename EmitFn>
void chunk_frame(uint64_t frame_id,
    const uint8_t* data,
    size_t len,
    size_t max_payload,
    EmitFn&& emit)
{
    const auto total = static_cast<uint16_t>((len + max_payload - 1) / max_payload);

    std::vector<uint8_t> buf(protocol::ChunkHeader::kWireSize + max_payload);

    for (uint16_t i = 0; i < total; ++i) {
        const size_t offset = static_cast<size_t>(i) * max_payload;
        const size_t chunk_len = std::min(max_payload, len - offset);

        protocol::ChunkHeader ch {
            .frame_id = frame_id,
            .chunk_idx = i,
            .total_chunks = total,
        };
        ch.serialize(buf.data());
        std::memcpy(buf.data() + protocol::ChunkHeader::kWireSize, data + offset,
            chunk_len);

        emit(buf.data(), protocol::ChunkHeader::kWireSize + chunk_len);
    }
}

class ChunkAssembler {
public:
    using CompleteCb = std::function<void(uint64_t frame_id, const uint8_t* data, size_t len)>;

    explicit ChunkAssembler(size_t max_payload,
        size_t max_frames = 8,
        uint16_t max_chunks_per_frame = 512)
        : max_payload_(max_payload)
        , max_frames_(max_frames)
        , max_chunks_per_frame_(max_chunks_per_frame)
    {
    }

    // Feed a raw wire packet (ChunkHeader + payload). Returns true if a frame
    // completed.
    bool push(const uint8_t* data, size_t len, const CompleteCb& on_complete)
    {
        if (len <= protocol::ChunkHeader::kWireSize) {
            return false;
        }

        const auto ch = protocol::ChunkHeader::deserialize(data);
        if (ch.total_chunks == 0 || ch.chunk_idx >= ch.total_chunks) {
            return false;
        }
        if (ch.total_chunks > max_chunks_per_frame_) {
            return false;
        }

        const uint8_t* payload = data + protocol::ChunkHeader::kWireSize;
        const size_t payload_len = len - protocol::ChunkHeader::kWireSize;

        auto& fa = frames_[ch.frame_id];
        if (fa.total == 0) {
            evict_old_(ch.frame_id);
            fa.total = ch.total_chunks;
            fa.buffer.resize(static_cast<size_t>(ch.total_chunks) * max_payload_);
            fa.received.assign(ch.total_chunks, false);
        }

        if (fa.total != ch.total_chunks) {
            return false;
        }

        if (!fa.received[ch.chunk_idx]) {
            std::memcpy(
                fa.buffer.data() + static_cast<size_t>(ch.chunk_idx) * max_payload_,
                payload, payload_len);
            fa.received[ch.chunk_idx] = true;
            ++fa.received_count;
            fa.actual_size = std::max(
                fa.actual_size,
                static_cast<size_t>(ch.chunk_idx) * max_payload_ + payload_len);
        }

        if (fa.received_count < fa.total) {
            return false;
        }

        fa.buffer.resize(fa.actual_size);
        on_complete(ch.frame_id, fa.buffer.data(), fa.actual_size);
        frames_.erase(ch.frame_id);
        return true;
    }

    size_t pending_frames() const { return frames_.size(); }

    void reset() { frames_.clear(); }

private:
    struct FrameAssembly {
        std::vector<uint8_t> buffer;
        std::vector<bool> received;
        uint16_t total = 0;
        uint16_t received_count = 0;
        size_t actual_size = 0;
    };

    void evict_old_(uint64_t current_frame_id)
    {
        for (auto it = frames_.begin(); it != frames_.end();) {
            if (it->first + max_frames_ < current_frame_id) {
                it = frames_.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t max_payload_;
    size_t max_frames_;
    uint16_t max_chunks_per_frame_;
    std::unordered_map<uint64_t, FrameAssembly> frames_;
};

} // namespace utils
