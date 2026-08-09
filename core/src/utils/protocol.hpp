#pragma once

#include "expected.hpp"
#include "identity.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace protocol {

namespace flags {
    inline constexpr uint32_t kTalkspurtStart = 1u << 0;
    inline constexpr uint32_t kKeyframe = 1u << 0;
}

struct AudioHeader {
    uint32_t seq = 0;
    uint32_t flags = 0;
    int64_t sender_ts_us = 0;

    static constexpr size_t kWireSize = 16;

    static std::optional<AudioHeader> deserialize(std::span<const uint8_t> src);
    void serialize(uint8_t* dst) const;
};

enum class VideoCodec : uint8_t {
    H264 = 0,
    HEVC = 1,
};

struct VideoHeader {
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t sender_ts_us = 0;
    uint32_t bitrate_kbps = 0;
    uint32_t frame_duration_us = 0;
    uint32_t flags = 0;
    VideoCodec codec = VideoCodec::H264;

    static constexpr size_t kWireSize = 32;

    static std::optional<VideoHeader> deserialize(std::span<const uint8_t> src);
    void serialize(uint8_t* dst) const;
};

struct FrameHeader {
    uint64_t frame_id = 0;

    static constexpr size_t kWireSize = 8;

    static std::optional<FrameHeader> deserialize(std::span<const uint8_t> src);
    void serialize(uint8_t* dst) const;
};

enum class RelayedMediaError {
    MissingSender,
    SenderTooLong,
    MissingPayload,
    PacketTooLarge,
    Truncated,
};

struct RelayedMediaPacket {
    driscord::PeerId sender_id;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
};

inline constexpr size_t kRelayedMediaSenderLenSize = 1;
inline constexpr size_t kMaxRelayedMediaSenderLen = std::numeric_limits<uint8_t>::max();

utils::Expected<std::string, RelayedMediaError> encode_relayed_media(
    const driscord::PeerId& sender_id,
    const uint8_t* payload,
    size_t payload_len);

utils::Expected<RelayedMediaPacket, RelayedMediaError> decode_relayed_media(
    const uint8_t* data,
    size_t len);

} // namespace protocol
