#include "protocol.hpp"

#include "byte_utils.hpp"

#include <cstring>

namespace protocol {

std::optional<AudioHeader> AudioHeader::deserialize(std::span<const uint8_t> src)
{
    if (src.size() < kWireSize) {
        return std::nullopt;
    }
    AudioHeader h;
    h.seq = utils::read_u32_le(src.data());
    h.flags = utils::read_u32_le(src.data() + 4);
    h.sender_ts_us = static_cast<int64_t>(utils::read_u64_le(src.data() + 8));
    return h;
}

void AudioHeader::serialize(uint8_t* dst) const
{
    utils::write_u32_le(dst, seq);
    utils::write_u32_le(dst + 4, flags);
    utils::write_u64_le(dst + 8, static_cast<uint64_t>(sender_ts_us));
}

std::optional<VideoHeader> VideoHeader::deserialize(std::span<const uint8_t> src)
{
    if (src.size() < kWireSize) {
        return std::nullopt;
    }
    const uint32_t codec_raw = utils::read_u32_le(src.data() + 28);
    if (codec_raw != static_cast<uint32_t>(VideoCodec::H264)
        && codec_raw != static_cast<uint32_t>(VideoCodec::HEVC)) {
        return std::nullopt;
    }
    VideoHeader h;
    h.width = utils::read_u32_le(src.data());
    h.height = utils::read_u32_le(src.data() + 4);
    h.sender_ts_us = static_cast<int64_t>(utils::read_u64_le(src.data() + 8));
    h.bitrate_kbps = utils::read_u32_le(src.data() + 16);
    h.frame_duration_us = utils::read_u32_le(src.data() + 20);
    h.flags = utils::read_u32_le(src.data() + 24);
    h.codec = static_cast<VideoCodec>(codec_raw);
    return h;
}

void VideoHeader::serialize(uint8_t* dst) const
{
    utils::write_u32_le(dst, width);
    utils::write_u32_le(dst + 4, height);
    utils::write_u64_le(dst + 8, static_cast<uint64_t>(sender_ts_us));
    utils::write_u32_le(dst + 16, bitrate_kbps);
    utils::write_u32_le(dst + 20, frame_duration_us);
    utils::write_u32_le(dst + 24, flags);
    utils::write_u32_le(dst + 28, static_cast<uint32_t>(codec));
}

std::optional<FrameHeader> FrameHeader::deserialize(std::span<const uint8_t> src)
{
    if (src.size() < kWireSize) {
        return std::nullopt;
    }
    FrameHeader h;
    h.frame_id = utils::read_u64_le(src.data());
    return h;
}

void FrameHeader::serialize(uint8_t* dst) const
{
    utils::write_u64_le(dst, frame_id);
}

utils::Expected<std::string, RelayedMediaError> encode_relayed_media(
    const driscord::PeerId& sender_id,
    const uint8_t* payload,
    size_t payload_len)
{
    if (sender_id.value.empty()) {
        return utils::Unexpected(RelayedMediaError::MissingSender);
    }
    if (sender_id.value.size() > kMaxRelayedMediaSenderLen) {
        return utils::Unexpected(RelayedMediaError::SenderTooLong);
    }
    if (!payload || payload_len == 0) {
        return utils::Unexpected(RelayedMediaError::MissingPayload);
    }

    const size_t header_len = kRelayedMediaSenderLenSize + sender_id.value.size();
    if (payload_len > std::string().max_size() - header_len) {
        return utils::Unexpected(RelayedMediaError::PacketTooLarge);
    }

    std::string out;
    out.resize(header_len + payload_len);
    auto* dst = reinterpret_cast<uint8_t*>(out.data());
    dst[0] = static_cast<uint8_t>(sender_id.value.size());
    std::memcpy(dst + kRelayedMediaSenderLenSize, sender_id.value.data(),
        sender_id.value.size());
    std::memcpy(dst + kRelayedMediaSenderLenSize + sender_id.value.size(), payload,
        payload_len);
    return out;
}

utils::Expected<RelayedMediaPacket, RelayedMediaError> decode_relayed_media(
    const uint8_t* data,
    size_t len)
{
    if (!data || len < kRelayedMediaSenderLenSize) {
        return utils::Unexpected(RelayedMediaError::Truncated);
    }

    const size_t sender_len = data[0];
    if (sender_len == 0) {
        return utils::Unexpected(RelayedMediaError::MissingSender);
    }
    if (len < kRelayedMediaSenderLenSize + sender_len) {
        return utils::Unexpected(RelayedMediaError::Truncated);
    }

    const size_t payload_offset = kRelayedMediaSenderLenSize + sender_len;
    if (len == payload_offset) {
        return utils::Unexpected(RelayedMediaError::MissingPayload);
    }

    RelayedMediaPacket packet;
    packet.sender_id = driscord::PeerId { std::string(
        reinterpret_cast<const char*>(data + kRelayedMediaSenderLenSize),
        sender_len) };
    packet.payload = data + payload_offset;
    packet.payload_len = len - payload_offset;
    return packet;
}

} // namespace protocol
