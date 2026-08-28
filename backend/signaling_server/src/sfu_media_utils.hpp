#pragma once

#include <rtc/rtc.hpp>

#include <cstdint>
#include <functional>
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

// SimulatedNetwork-style link model applied after the loss/reorder stage:
// a single-packet capacity link (serialization delay for free) feeding a
// delay queue with normally distributed jitter. The parameter semantics
// mirror WebRTC's BuiltInNetworkBehaviorConfig. Zero values are the
// production default: schedule_packet_departure() then returns the arrival
// time unchanged and the routers keep the inline forwarding path with no
// timers or buffering.
struct LinkModelConfig {
    // Base one-way queueing delay and its standard deviation.
    int queue_delay_ms = 0;
    int delay_standard_deviation_ms = 0;
    // Bound on packets waiting in the delay queue; 0 = unbounded.
    size_t queue_length_packets = 0;
    // Link speed for the serialization delay; 0 = infinite.
    uint32_t link_capacity_kbps = 0;
    // With the default false, departures are clamped monotonic per track so
    // jitter never doubles as a hidden reorder fault (netem's classic trap).
    bool allow_reordering = false;
    uint32_t duplicate_every_nth = 0;
    // Seed for the per-track deterministic PRNG; tests pin it for repro.
    uint64_t seed = 0;

    [[nodiscard]] bool enabled() const noexcept
    {
        return queue_delay_ms > 0 || delay_standard_deviation_ms > 0
            || link_capacity_kbps > 0 || duplicate_every_nth > 0;
    }
};

struct LinkModelState {
    bool initialized = false;
    uint64_t rng = 0;
    uint64_t packets_seen = 0;
    // The single packet occupying the capacity link is busy until here.
    int64_t capacity_busy_until_us = 0;
    int64_t last_departure_us = 0;
    // Packets currently held in the caller's delay queue. The scheduler
    // increments it for every deferred departure; the caller decrements when
    // a deferred packet actually leaves.
    size_t queued_packets = 0;
};

struct LinkScheduleResult {
    int64_t departure_us = 0;
    // Emit a second copy of the packet at the same departure time.
    bool duplicate = false;
};

// Pure and deterministic per (config, state, inputs): decides when a packet
// leaves the emulated link, or nullopt when the bounded queue overflows and
// the packet is dropped. Zero-config identity: returns arrival_us unchanged.
[[nodiscard]] std::optional<LinkScheduleResult> schedule_packet_departure(
    const LinkModelConfig& config,
    LinkModelState& state,
    int64_t arrival_us,
    size_t packet_bytes);

// Deterministic post-SRTP impairment used by the real client <-> SFU
// integration gate. Zero values are the production default and add no packet
// buffering. Counters are independent per publisher/media track.
//
// RTCP is exempt from every stage here, including the link model and
// link_down — the invariant the RTCP classification tests pin. A real
// network delays RTCP too; that fidelity deliberately belongs to the
// kernel-level netem tier, not to this in-process stage.
struct RtpFaultConfig {
    uint32_t drop_every_nth = 0;
    uint32_t reorder_every_nth = 0;
    BurstLossConfig burst;
    LinkModelConfig link;
    // Hard blackout: media is dropped entirely while set. Toggled mid-call
    // through WebSocketServer::update_fault_config for reconnection and
    // recovery scenarios.
    bool link_down = false;
    // Test-only observation of every packet entering the forwarding fault
    // stage, before any fault is applied — the hook the rtpdump fixture
    // recorder uses to capture real publisher traffic off the production
    // path. Runs on media receive threads, so the callback must be
    // thread-safe. Empty (and free) in production.
    std::function<void(const rtc::binary&)> packet_tap;
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
