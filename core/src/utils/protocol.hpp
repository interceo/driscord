#pragma once

#include <cstddef>
#include <cstdint>

#include "byte_utils.hpp"

namespace protocol {

// Flags shared by the media headers below.
namespace flags {
    // Audio: first packet of a talkspurt. The sender stops emitting during DTX and
    // noise-gated silence, so without this bit a receiver cannot tell deliberate
    // silence from packet loss and would conceal the pause with PLC noise.
    inline constexpr uint32_t kTalkspurtStart = 1u << 0;
    // Video: this frame is a keyframe.
    inline constexpr uint32_t kKeyframe = 1u << 0;
}

struct AudioHeader {
    uint32_t seq = 0;
    uint32_t flags = 0;
    // utils::SenderClock timeline, shared with VideoHeader::sender_ts_us.
    int64_t sender_ts_us = 0;

    static constexpr size_t kWireSize = 16;

    static AudioHeader deserialize(const uint8_t* src)
    {
        AudioHeader h;
        h.seq = utils::read_u32_le(src);
        h.flags = utils::read_u32_le(src + 4);
        h.sender_ts_us = static_cast<int64_t>(utils::read_u64_le(src + 8));
        return h;
    }

    void serialize(uint8_t* dst) const
    {
        utils::write_u32_le(dst, seq);
        utils::write_u32_le(dst + 4, flags);
        utils::write_u64_le(dst + 8, static_cast<uint64_t>(sender_ts_us));
    }
};

enum class VideoCodec : uint8_t {
    H264 = 0,
    HEVC = 1,
};

struct VideoHeader {
    uint32_t width = 0;
    uint32_t height = 0;
    // utils::SenderClock timeline, shared with AudioHeader::sender_ts_us.
    int64_t sender_ts_us = 0;
    uint32_t bitrate_kbps = 0;
    uint32_t frame_duration_us = 0;
    uint32_t flags = 0;
    VideoCodec codec = VideoCodec::H264;

    static constexpr size_t kWireSize = 32;

    static VideoHeader deserialize(const uint8_t* src)
    {
        VideoHeader h;
        h.width = utils::read_u32_le(src);
        h.height = utils::read_u32_le(src + 4);
        h.sender_ts_us = static_cast<int64_t>(utils::read_u64_le(src + 8));
        h.bitrate_kbps = utils::read_u32_le(src + 16);
        h.frame_duration_us = utils::read_u32_le(src + 20);
        h.flags = utils::read_u32_le(src + 24);
        h.codec = static_cast<VideoCodec>(utils::read_u32_le(src + 28));
        return h;
    }

    void serialize(uint8_t* dst) const
    {
        utils::write_u32_le(dst, width);
        utils::write_u32_le(dst + 4, height);
        utils::write_u64_le(dst + 8, static_cast<uint64_t>(sender_ts_us));
        utils::write_u32_le(dst + 16, bitrate_kbps);
        utils::write_u32_le(dst + 20, frame_duration_us);
        utils::write_u32_le(dst + 24, flags);
        utils::write_u32_le(dst + 28, static_cast<uint32_t>(codec));
    }
};

struct ChunkHeader {
    uint64_t frame_id = 0;
    uint16_t chunk_idx = 0;
    uint16_t total_chunks = 0;

    static constexpr size_t kWireSize = 12;

    static ChunkHeader deserialize(const uint8_t* src)
    {
        ChunkHeader h;
        h.frame_id = utils::read_u64_le(src);
        h.chunk_idx = utils::read_u16_le(src + 8);
        h.total_chunks = utils::read_u16_le(src + 10);
        return h;
    }

    void serialize(uint8_t* dst) const
    {
        utils::write_u64_le(dst, frame_id);
        utils::write_u16_le(dst + 8, chunk_idx);
        utils::write_u16_le(dst + 10, total_chunks);
    }
};

} // namespace protocol
