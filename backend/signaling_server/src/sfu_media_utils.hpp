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

// Gilbert-Elliott burst-loss model. Unlike the counter-based `drop_every_nth`,
// this alternates between a good and a bad state, so losses arrive in bursts —
// the shape real networks produce and the one NACK/PLI recovery actually has
// to survive. Disabled by default (transition into the bad state is zero).
// The field set mirrors WebRTC's BuiltInNetworkBehaviorConfig loss knobs.
struct BurstLossConfig {
    // Per-packet state-transition probabilities, in [0, 1].
    double good_to_bad = 0.0; // enter a loss burst
    double bad_to_good = 1.0; // leave a loss burst
    // Loss probability while in each state, in [0, 1].
    double loss_in_good = 0.0;
    double loss_in_bad = 1.0;
    // Seed for the per-track deterministic PRNG; tests pin it for repro.
    uint64_t seed = 0;

    [[nodiscard]] bool enabled() const noexcept
    {
        return good_to_bad > 0.0 || loss_in_good > 0.0;
    }
};

// Deterministic post-SRTP impairment used by the real client <-> SFU
// integration gate. Zero values are the production default and add no packet
// buffering. Counters are independent per publisher/media track.
struct RtpFaultConfig {
    uint32_t drop_every_nth = 0;
    uint32_t reorder_every_nth = 0;
    BurstLossConfig burst;
};

struct RtpFaultState {
    uint64_t packets_seen = 0;
    std::optional<rtc::binary> delayed_packet;
    // Gilbert-Elliott running state.
    bool burst_initialized = false;
    bool in_bad_state = false;
    uint64_t rng = 0;
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
