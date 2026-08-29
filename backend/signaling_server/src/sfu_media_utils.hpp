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

struct BurstLossConfig {
    double good_to_bad = 0.0;
    double bad_to_good = 1.0;
    double loss_in_good = 0.0;
    double loss_in_bad = 1.0;
    uint64_t seed = 0;

    [[nodiscard]] bool enabled() const noexcept
    {
        return good_to_bad > 0.0 || loss_in_good > 0.0;
    }
};

struct LinkModelConfig {
    int queue_delay_ms = 0;
    int delay_standard_deviation_ms = 0;
    size_t queue_length_packets = 0;
    uint32_t link_capacity_kbps = 0;
    bool allow_reordering = false;
    uint32_t duplicate_every_nth = 0;
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
    int64_t capacity_busy_until_us = 0;
    int64_t last_departure_us = 0;
    size_t queued_packets = 0;
};

struct LinkScheduleResult {
    int64_t departure_us = 0;
    bool duplicate = false;
};

[[nodiscard]] std::optional<LinkScheduleResult> schedule_packet_departure(
    const LinkModelConfig& config,
    LinkModelState& state,
    int64_t arrival_us,
    size_t packet_bytes);

struct RtpFaultConfig {
    uint32_t drop_every_nth = 0;
    uint32_t reorder_every_nth = 0;
    BurstLossConfig burst;
    LinkModelConfig link;
    bool link_down = false;
    std::function<void(const rtc::binary&)> packet_tap;
};

struct RtpFaultState {
    uint64_t packets_seen = 0;
    std::optional<rtc::binary> delayed_packet;
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

void apply_forwarding_feedback_policy(rtc::Description::Media& description);

void remove_auxiliary_video_codecs(rtc::Description::Media& description);

[[nodiscard]] uint32_t allocate_slot_ssrc() noexcept;

[[nodiscard]] std::optional<uint8_t> mid_extension_id(
    rtc::Description::Media description) noexcept;

[[nodiscard]] RtpFormat primary_rtp_format(
    const rtc::Description::Media& description,
    std::string_view media_type) noexcept;

}
