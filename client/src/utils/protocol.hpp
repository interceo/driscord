#pragma once

#include <cstddef>
#include <cstdint>

#include "byte_utils.hpp"
#include "time.hpp"

namespace protocol {

// Audio packet wire layout: [seq:8][sender_ts:8][opus_data:N]
struct AudioHeader {
    uint64_t seq = 0;
    utils::WallTimestamp sender_ts{};

    static constexpr size_t kWireSize = 16;

    static AudioHeader deserialize(const uint8_t* src) {
        AudioHeader h;
        h.seq = utils::read_u64_le(src);
        h.sender_ts = utils::WallFromMs(utils::read_u64_le(src + 8));
        return h;
    }

    void serialize(uint8_t* dst) const {
        utils::write_u64_le(dst, seq);
        utils::write_u64_le(dst + 8, utils::WallToMs(sender_ts));
    }
};

// Video frame envelope wire layout:
//   [width:4][height:4][sender_ts:8][bitrate_kbps:4][frame_duration_us:4]
struct VideoHeader {
    uint32_t width = 0;
    uint32_t height = 0;
    utils::WallTimestamp sender_ts{};
    uint32_t bitrate_kbps = 0;
    uint32_t frame_duration_us = 0;

    static constexpr size_t kWireSize = 24;

    static VideoHeader deserialize(const uint8_t* src) {
        VideoHeader h;
        h.width = utils::read_u32_le(src);
        h.height = utils::read_u32_le(src + 4);
        h.sender_ts = utils::WallFromMs(utils::read_u64_le(src + 8));
        h.bitrate_kbps = utils::read_u32_le(src + 16);
        h.frame_duration_us = utils::read_u32_le(src + 20);
        return h;
    }

    void serialize(uint8_t* dst) const {
        utils::write_u32_le(dst, width);
        utils::write_u32_le(dst + 4, height);
        utils::write_u64_le(dst + 8, utils::WallToMs(sender_ts));
        utils::write_u32_le(dst + 16, bitrate_kbps);
        utils::write_u32_le(dst + 20, frame_duration_us);
    }
};

// Wire size sanity checks: verify kWireSize matches the sum of serialized field sizes.
static_assert(AudioHeader::kWireSize == 2 * sizeof(uint64_t), "AudioHeader wire layout: seq(8) + sender_ts(8)");
static_assert(
    VideoHeader::kWireSize == 2 * sizeof(uint32_t) + sizeof(uint64_t) + 2 * sizeof(uint32_t),
    "VideoHeader wire layout: width(4) + height(4) + sender_ts(8) + bitrate_kbps(4) + frame_duration_us(4)"
);

}  // namespace protocol
