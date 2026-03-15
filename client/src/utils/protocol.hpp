#pragma once

#include <cstddef>
#include <cstdint>

#include "byte_utils.hpp"
#include "time.hpp"

namespace protocol {

// Audio packet wire layout: [seq:8][sender_ts:8][opus_data:N]
struct AudioHeader {
    uint64_t seq = 0;
    drist::WallTimestamp sender_ts{};

    static constexpr size_t kWireSize = 16;

    static AudioHeader deserialize(const uint8_t* src) {
        AudioHeader h;
        h.seq = drist::read_u64_le(src);
        h.sender_ts = drist::WallFromMs(drist::read_u64_le(src + 8));
        return h;
    }

    void serialize(uint8_t* dst) const {
        drist::write_u64_le(dst, seq);
        drist::write_u64_le(dst + 8, drist::WallToMs(sender_ts));
    }
};

// Video frame envelope wire layout: [width:4][height:4][sender_ts:8][bitrate_kbps:4][_pad:4]
// _pad ensures total size is a multiple of 8 (natural alignment for sender_ts on wire).
struct VideoHeader {
    uint32_t width = 0;
    uint32_t height = 0;
    drist::WallTimestamp sender_ts{};
    uint32_t bitrate_kbps = 0;

    static constexpr size_t kWireSize = 24;

    static VideoHeader deserialize(const uint8_t* src) {
        VideoHeader h;
        h.width = drist::read_u32_le(src);
        h.height = drist::read_u32_le(src + 4);
        h.sender_ts = drist::WallFromMs(drist::read_u64_le(src + 8));
        h.bitrate_kbps = drist::read_u32_le(src + 16);
        return h;
    }

    void serialize(uint8_t* dst) const {
        drist::write_u32_le(dst, width);
        drist::write_u32_le(dst + 4, height);
        drist::write_u64_le(dst + 8, drist::WallToMs(sender_ts));
        drist::write_u32_le(dst + 16, bitrate_kbps);
        drist::write_u32_le(dst + 20, 0);  // _pad
    }
};

// Video chunk wire layout: [frame_id:2][chunk_idx:2][total_chunks:2][payload:N]
struct ChunkHeader {
    uint16_t frame_id = 0;
    uint16_t chunk_idx = 0;
    uint16_t total_chunks = 0;

    static constexpr size_t kWireSize = 6;

    static ChunkHeader deserialize(const uint8_t* src) {
        ChunkHeader h;
        h.frame_id = drist::read_u16_le(src);
        h.chunk_idx = drist::read_u16_le(src + 2);
        h.total_chunks = drist::read_u16_le(src + 4);
        return h;
    }

    void serialize(uint8_t* dst) const {
        drist::write_u16_le(dst, frame_id);
        drist::write_u16_le(dst + 2, chunk_idx);
        drist::write_u16_le(dst + 4, total_chunks);
    }
};

constexpr size_t kMaxChunkPayload = 60000;

// Wire size sanity checks: verify kWireSize matches the sum of serialized field sizes.
static_assert(AudioHeader::kWireSize == 2 * sizeof(uint64_t), "AudioHeader wire layout: seq(8) + sender_ts(8)");
static_assert(
    VideoHeader::kWireSize == 2 * sizeof(uint32_t) + sizeof(uint64_t) + 2 * sizeof(uint32_t),
    "VideoHeader wire layout: width(4) + height(4) + sender_ts(8) + bitrate_kbps(4) + _pad(4)"
);
static_assert(
    ChunkHeader::kWireSize == 3 * sizeof(uint16_t),
    "ChunkHeader wire layout: frame_id(2) + chunk_idx(2) + total_chunks(2)"
);

}  // namespace protocol
