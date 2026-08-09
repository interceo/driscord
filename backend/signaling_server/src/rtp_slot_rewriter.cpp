#include "rtp_slot_rewriter.hpp"

#include <algorithm>
#include <cstddef>

namespace driscord::sfu {
namespace {

    uint8_t byte_at(const rtc::binary& packet, size_t offset)
    {
        return std::to_integer<uint8_t>(packet[offset]);
    }

    uint16_t network_u16(const rtc::binary& packet, size_t offset)
    {
        return static_cast<uint16_t>((byte_at(packet, offset) << 8)
            | byte_at(packet, offset + 1));
    }

    uint32_t network_u32(const rtc::binary& packet, size_t offset)
    {
        return (static_cast<uint32_t>(byte_at(packet, offset)) << 24)
            | (static_cast<uint32_t>(byte_at(packet, offset + 1)) << 16)
            | (static_cast<uint32_t>(byte_at(packet, offset + 2)) << 8)
            | static_cast<uint32_t>(byte_at(packet, offset + 3));
    }

    void set_network_u16(rtc::binary& packet, size_t offset, uint16_t value)
    {
        packet[offset] = std::byte((value >> 8) & 0xff);
        packet[offset + 1] = std::byte(value & 0xff);
    }

    void set_network_u32(rtc::binary& packet, size_t offset, uint32_t value)
    {
        packet[offset] = std::byte((value >> 24) & 0xff);
        packet[offset + 1] = std::byte((value >> 16) & 0xff);
        packet[offset + 2] = std::byte((value >> 8) & 0xff);
        packet[offset + 3] = std::byte(value & 0xff);
    }

    bool sequence_is_newer(uint16_t value, uint16_t reference)
    {
        return static_cast<int16_t>(value - reference) > 0;
    }

    bool clear_one_byte_mid(rtc::binary& packet,
        size_t begin,
        size_t end,
        uint8_t mid_id)
    {
        size_t cursor = begin;
        while (cursor < end) {
            const uint8_t descriptor = byte_at(packet, cursor);
            if (descriptor == 0) {
                ++cursor;
                continue;
            }
            const uint8_t id = descriptor >> 4;
            if (id == 15) {
                return true;
            }
            const size_t value_size = (descriptor & 0x0f) + 1;
            if (cursor + 1 + value_size > end) {
                return false;
            }
            if (id == mid_id) {
                std::fill(packet.begin() + static_cast<ptrdiff_t>(cursor),
                    packet.begin()
                        + static_cast<ptrdiff_t>(cursor + 1 + value_size),
                    std::byte { 0 });
                return true;
            }
            cursor += 1 + value_size;
        }
        return true;
    }

    bool clear_two_byte_mid(rtc::binary& packet,
        size_t begin,
        size_t end,
        uint8_t mid_id)
    {
        size_t cursor = begin;
        while (cursor < end) {
            const uint8_t id = byte_at(packet, cursor);
            if (id == 0) {
                ++cursor;
                continue;
            }
            if (cursor + 2 > end) {
                return false;
            }
            const size_t value_size = byte_at(packet, cursor + 1);
            if (cursor + 2 + value_size > end) {
                return false;
            }
            if (id == mid_id) {
                std::fill(packet.begin() + static_cast<ptrdiff_t>(cursor),
                    packet.begin()
                        + static_cast<ptrdiff_t>(cursor + 2 + value_size),
                    std::byte { 0 });
                return true;
            }
            cursor += 2 + value_size;
        }
        return true;
    }

} // namespace

bool rewrite_rtp_for_slot(rtc::binary& packet,
    uint32_t output_ssrc,
    std::optional<uint8_t> publisher_mid_extension_id) noexcept
{
    constexpr size_t kFixedHeaderSize = 12;
    if (packet.size() < kFixedHeaderSize || (byte_at(packet, 0) >> 6) != 2
        || rtc::IsRtcp(packet)) {
        return false;
    }

    const size_t csrc_size = static_cast<size_t>(byte_at(packet, 0) & 0x0f) * 4;
    const size_t header_size = kFixedHeaderSize + csrc_size;
    if (header_size > packet.size()) {
        return false;
    }

    const bool has_extension = (byte_at(packet, 0) & 0x10) != 0;
    size_t payload_begin = header_size;
    if (has_extension) {
        if (header_size + 4 > packet.size()) {
            return false;
        }
        const uint16_t profile = network_u16(packet, header_size);
        const size_t extension_size
            = static_cast<size_t>(network_u16(packet, header_size + 2)) * 4;
        const size_t body_begin = header_size + 4;
        if (extension_size > packet.size() - body_begin) {
            return false;
        }
        const size_t body_end = body_begin + extension_size;
        payload_begin = body_end;
        if (publisher_mid_extension_id) {
            bool valid = true;
            if (profile == 0xbede) {
                valid = clear_one_byte_mid(packet, body_begin, body_end,
                    *publisher_mid_extension_id);
            } else if ((profile & 0xfff0) == 0x1000) {
                valid = clear_two_byte_mid(packet, body_begin, body_end,
                    *publisher_mid_extension_id);
            }
            if (!valid) {
                return false;
            }
        }
    }

    // libdatachannel's RtcpSrReporter asserts on padded RTP. Strip valid
    // padding before the packet reaches its media-handler chain while keeping
    // the RTP packet (and therefore sequence continuity) intact.
    if ((byte_at(packet, 0) & 0x20) != 0) {
        if (packet.size() <= payload_begin) {
            return false;
        }
        const size_t padding_size = byte_at(packet, packet.size() - 1);
        if (padding_size == 0 || padding_size > packet.size() - payload_begin) {
            return false;
        }
        packet.resize(packet.size() - padding_size);
        packet[0] = std::byte { static_cast<uint8_t>(byte_at(packet, 0) & ~0x20) };
    }

    set_network_u32(packet, 8, output_ssrc);
    return true;
}

RtpSlotRewriter::RtpSlotRewriter(
    uint32_t clock_rate, uint32_t default_timestamp_step)
    : clock_rate_(clock_rate)
    , timestamp_step_(default_timestamp_step)
{
}

uint64_t RtpSlotRewriter::begin_source()
{
    std::scoped_lock lock(mutex_);
    ++generation_;
    source_initialized_ = false;
    return generation_;
}

void RtpSlotRewriter::end_source()
{
    std::scoped_lock lock(mutex_);
    ++generation_;
    source_initialized_ = false;
}

bool RtpSlotRewriter::rewrite(rtc::binary& packet,
    uint64_t source_generation,
    uint32_t output_ssrc,
    std::optional<uint8_t> publisher_mid_extension_id)
{
    if (!rewrite_rtp_for_slot(
            packet, output_ssrc, publisher_mid_extension_id)) {
        return false;
    }

    const uint16_t input_sequence = network_u16(packet, 2);
    const uint32_t input_timestamp = network_u32(packet, 4);
    std::scoped_lock lock(mutex_);
    if (source_generation != generation_) {
        return false;
    }

    bool first_for_source = false;
    if (!source_initialized_) {
        source_anchor_sequence_ = input_sequence;
        if (output_initialized_) {
            const uint16_t next_sequence = latest_output_sequence_ + 1;
            const uint32_t next_timestamp
                = latest_output_timestamp_ + timestamp_step_;
            sequence_offset_ = next_sequence - input_sequence;
            timestamp_offset_ = next_timestamp - input_timestamp;
        } else {
            sequence_offset_ = 0;
            timestamp_offset_ = 0;
        }
        source_initialized_ = true;
        first_for_source = true;
        latest_input_sequence_ = input_sequence;
        latest_input_timestamp_ = input_timestamp;
    } else if (static_cast<int16_t>(
                   input_sequence - source_anchor_sequence_)
        < 0) {
        // This packet predates the first packet accepted after reassignment and
        // would collide with the previous publisher's output sequence space.
        return false;
    }

    const uint16_t output_sequence = input_sequence + sequence_offset_;
    const uint32_t output_timestamp = input_timestamp + timestamp_offset_;
    set_network_u16(packet, 2, output_sequence);
    set_network_u32(packet, 4, output_timestamp);

    if (first_for_source || !output_initialized_
        || sequence_is_newer(input_sequence, latest_input_sequence_)) {
        const uint32_t input_delta = input_timestamp - latest_input_timestamp_;
        if (input_delta > 0 && input_delta <= clock_rate_ * 10) {
            timestamp_step_ = input_delta;
        }
        latest_input_sequence_ = input_sequence;
        latest_input_timestamp_ = input_timestamp;
        latest_output_sequence_ = output_sequence;
        latest_output_timestamp_ = output_timestamp;
        output_initialized_ = true;
    }
    return true;
}

} // namespace driscord::sfu
