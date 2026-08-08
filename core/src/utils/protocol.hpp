#pragma once

#include "identity.hpp"
#include "expected.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

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

    static AudioHeader deserialize(const uint8_t* src);
    void serialize(uint8_t* dst) const;
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

    static VideoHeader deserialize(const uint8_t* src);
    void serialize(uint8_t* dst) const;
};

// Prepended to every encoded video frame. Frames are sent as one DataChannel
// message and fragmented by SCTP, so there is no application-level chunking —
// this only carries the sequence number the receiver reorders on.
struct FrameHeader {
    uint64_t frame_id = 0;

    static constexpr size_t kWireSize = 8;

    static FrameHeader deserialize(const uint8_t* src);
    void serialize(uint8_t* dst) const;
};

// Server -> client media framing: u8 sender-id length, sender id, payload.
// The DataChannel label carries the media kind; this wrapper only identifies
// the peer that originally sent the payload.
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
inline constexpr size_t kMaxRelayedMediaSenderLen =
    std::numeric_limits<uint8_t>::max();

utils::Expected<std::string, RelayedMediaError> encode_relayed_media(
    const driscord::PeerId& sender_id,
    const uint8_t* payload,
    size_t payload_len);

utils::Expected<RelayedMediaPacket, RelayedMediaError> decode_relayed_media(
    const uint8_t* data,
    size_t len);

} // namespace protocol
