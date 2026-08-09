#pragma once

#include <rtc/rtc.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace driscord::sfu {

struct RtpFormat {
    uint8_t payload_type = 0;
    uint32_t clock_rate = 0;
};

// Deterministic post-SRTP impairment used by the real client <-> SFU
// integration gate. Zero values are the production default and add no packet
// buffering. Counters are independent per publisher/media track.
struct RtpFaultConfig {
    uint32_t drop_every_nth = 0;
    uint32_t reorder_every_nth = 0;
};

struct RtpFaultState {
    uint64_t packets_seen = 0;
    std::optional<rtc::binary> delayed_packet;
};

struct RtpFaultResult {
    std::optional<rtc::binary> first;
    std::optional<rtc::binary> second;
};

[[nodiscard]] RtpFaultResult apply_rtp_faults(
    const RtpFaultConfig& config,
    RtpFaultState& state,
    rtc::binary packet);

// Removes transport feedback that cannot be represented honestly after one
// publisher transport is fanned out into independent subscriber transports.
// Hop-local RR, NACK and video PLI/FIR remain negotiated.
void apply_forwarding_feedback_policy(rtc::Description::Media& description);

// RTX/FEC carry independent SSRC/sequence spaces. Until the router advertises
// and rewrites an explicit SSRC-group per output slot, negotiate primary video
// RTP only; hop-local NACK still retransmits cached primary packets.
void remove_auxiliary_video_codecs(rtc::Description::Media& description);

[[nodiscard]] uint32_t allocate_slot_ssrc() noexcept;

[[nodiscard]] std::optional<uint8_t> mid_extension_id(
    rtc::Description::Media description) noexcept;

// Selects a primary codec rather than RTX/RED/FEC. The supplied fallbacks are
// only defensive; matching Google WebRTC offers always contain Opus for audio
// and at least one real-time video codec.
[[nodiscard]] RtpFormat primary_rtp_format(
    const rtc::Description::Media& description,
    std::string_view media_type) noexcept;

} // namespace driscord::sfu
