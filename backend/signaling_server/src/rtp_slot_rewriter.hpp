#pragma once

#include <rtc/rtc.hpp>

#include <cstdint>
#include <mutex>
#include <optional>

namespace driscord::sfu {

// Applies the stable SSRC of an SFU output slot and removes the publisher MID.
// Sequence number and timestamp are intentionally left unchanged; use
// RtpSlotRewriter for a slot that can be reassigned between publishers.
bool rewrite_rtp_for_slot(rtc::binary& packet,
    uint32_t output_ssrc,
    std::optional<uint8_t> publisher_mid_extension_id) noexcept;

// Maintains one continuous RTP sequence/timestamp space for a long-lived
// subscriber slot. The generation token rejects packets that raced with a
// publisher reassignment.
class RtpSlotRewriter final {
public:
    RtpSlotRewriter(uint32_t clock_rate, uint32_t default_timestamp_step);

    RtpSlotRewriter(const RtpSlotRewriter&) = delete;
    RtpSlotRewriter& operator=(const RtpSlotRewriter&) = delete;

    [[nodiscard]] uint64_t begin_source();
    void end_source();
    bool rewrite(rtc::binary& packet,
        uint64_t source_generation,
        uint32_t output_ssrc,
        std::optional<uint8_t> publisher_mid_extension_id);

private:
    const uint32_t clock_rate_;
    uint32_t timestamp_step_;
    std::mutex mutex_;
    uint64_t generation_ = 0;
    bool source_initialized_ = false;
    bool output_initialized_ = false;
    uint16_t source_anchor_sequence_ = 0;
    uint16_t latest_input_sequence_ = 0;
    uint16_t latest_output_sequence_ = 0;
    uint16_t sequence_offset_ = 0;
    uint32_t latest_input_timestamp_ = 0;
    uint32_t latest_output_timestamp_ = 0;
    uint32_t timestamp_offset_ = 0;
};

} // namespace driscord::sfu
