#include "sfu_media_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace driscord::sfu {
namespace {

    constexpr std::string_view kMidExtensionUri = "urn:ietf:params:rtp-hdrext:sdes:mid";
    constexpr std::string_view kTransportSequenceNumberUri = "http://www.ietf.org/id/"
                                                             "draft-holmer-rmcat-transport-wide-cc-extensions-01";
    constexpr std::string_view kTransportSequenceNumberV2Uri = "http://www.webrtc.org/experiments/rtp-hdrext/transport-wide-cc-02";

    std::string lowercase(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool is_auxiliary_video_format(std::string_view format)
    {
        static const std::unordered_set<std::string_view> auxiliary {
            "red", "rtx", "ulpfec", "flexfec-03"
        };
        return auxiliary.contains(format);
    }

    rtc::Description::Media::RtpMap* try_rtp_map(
        rtc::Description::Media& description, int payload_type) noexcept
    {
        try {
            return description.rtpMap(payload_type);
        } catch (const std::invalid_argument&) {
            return nullptr;
        }
    }

    const rtc::Description::Media::RtpMap* try_rtp_map(
        const rtc::Description::Media& description, int payload_type) noexcept
    {
        try {
            return description.rtpMap(payload_type);
        } catch (const std::invalid_argument&) {
            return nullptr;
        }
    }

    // splitmix64: a tiny, well-distributed PRNG. Deterministic given a seed,
    // so a burst-loss test reproduces exactly across runs and machines.
    uint64_t next_random(uint64_t& state)
    {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Uniform double in [0, 1).
    double next_unit(uint64_t& state)
    {
        return static_cast<double>(next_random(state) >> 11)
            / static_cast<double>(1ull << 53);
    }

    // One Gilbert-Elliott step: decides loss for this packet, then advances
    // between the good and bad states. Returns true if the packet is lost.
    bool burst_should_drop(const BurstLossConfig& burst, RtpFaultState& state)
    {
        if (!state.burst_initialized) {
            state.rng = burst.seed + 0xD1B54A32D192ED03ull;
            state.in_bad_state = false;
            state.burst_initialized = true;
        }
        const double loss_probability
            = state.in_bad_state ? burst.loss_in_bad : burst.loss_in_good;
        const bool lost = next_unit(state.rng) < loss_probability;
        const double transition = state.in_bad_state ? burst.bad_to_good
                                                     : burst.good_to_bad;
        if (next_unit(state.rng) < transition) {
            state.in_bad_state = !state.in_bad_state;
        }
        return lost;
    }

    // Box-Muller: standard normal deviate from two uniform draws. Purely a
    // function of the PRNG state, so schedules reproduce exactly per seed.
    double next_gaussian(uint64_t& state)
    {
        constexpr double kPi = 3.14159265358979323846;
        const double u1 = std::max(next_unit(state), 1e-12);
        const double u2 = next_unit(state);
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
    }

} // namespace

std::optional<LinkScheduleResult> schedule_packet_departure(
    const LinkModelConfig& config,
    LinkModelState& state,
    int64_t arrival_us,
    size_t packet_bytes)
{
    if (!config.enabled()) {
        return LinkScheduleResult { .departure_us = arrival_us };
    }
    if (!state.initialized) {
        state.rng = config.seed + 0x9E3779B97F4A7C15ull;
        state.initialized = true;
    }
    ++state.packets_seen;

    // Serialization on the single-packet capacity link.
    const int64_t start = std::max(arrival_us, state.capacity_busy_until_us);
    const int64_t serialization_us = config.link_capacity_kbps == 0
        ? 0
        : static_cast<int64_t>(packet_bytes) * 8'000
            / static_cast<int64_t>(config.link_capacity_kbps);
    int64_t departure = start + serialization_us;

    if (config.queue_delay_ms > 0 || config.delay_standard_deviation_ms > 0) {
        const double jitter_ms = config.delay_standard_deviation_ms > 0
            ? next_gaussian(state.rng) * config.delay_standard_deviation_ms
            : 0.0;
        const double delay_ms
            = std::max(0.0, static_cast<double>(config.queue_delay_ms) + jitter_ms);
        departure += static_cast<int64_t>(delay_ms * 1'000.0);
    }
    if (!config.allow_reordering) {
        departure = std::max(departure, state.last_departure_us);
    }

    if (departure > arrival_us && config.queue_length_packets != 0
        && state.queued_packets >= config.queue_length_packets) {
        return std::nullopt;
    }
    state.capacity_busy_until_us = start + serialization_us;
    state.last_departure_us = departure;
    if (departure > arrival_us) {
        ++state.queued_packets;
    }
    return LinkScheduleResult {
        .departure_us = departure,
        .duplicate = config.duplicate_every_nth != 0
            && state.packets_seen % config.duplicate_every_nth == 0,
    };
}

RtpFaultResult apply_rtp_faults(const RtpFaultConfig& config,
    RtpFaultState& state,
    rtc::binary packet)
{
    if (config.packet_tap) {
        config.packet_tap(packet);
    }
    if (rtc::IsRtcp(packet)
        || (config.drop_every_nth == 0 && config.reorder_every_nth == 0
            && !config.burst.enabled())) {
        return { .first = std::move(packet), .second = std::nullopt };
    }

    ++state.packets_seen;
    // Burst loss is evaluated on every packet so its state machine stays
    // coherent, but it never touches reordering.
    const bool burst_drop
        = config.burst.enabled() && burst_should_drop(config.burst, state);
    if (burst_drop
        || (config.drop_every_nth != 0
            && state.packets_seen % config.drop_every_nth == 0)) {
        return { };
    }

    if (state.delayed_packet) {
        RtpFaultResult result {
            .first = std::move(packet),
            .second = std::move(*state.delayed_packet),
        };
        state.delayed_packet.reset();
        return result;
    }

    if (config.reorder_every_nth != 0
        && state.packets_seen % config.reorder_every_nth == 0) {
        state.delayed_packet = std::move(packet);
        return { };
    }
    return { .first = std::move(packet), .second = std::nullopt };
}

void apply_forwarding_feedback_policy(rtc::Description::Media& description)
{
    for (const int payload_type : description.payloadTypes()) {
        if (auto* mapping = try_rtp_map(description, payload_type)) {
            mapping->removeFeedback("transport-cc");
            mapping->removeFeedback("goog-remb");
        }
    }
    for (const int id : description.extIds()) {
        const auto* extension = description.extMap(id);
        if (extension && (extension->uri == kTransportSequenceNumberUri || extension->uri == kTransportSequenceNumberV2Uri)) {
            description.removeExtMap(id);
        }
    }
}

void remove_auxiliary_video_codecs(rtc::Description::Media& description)
{
    description.removeFormat("rtx");
    description.removeFormat("red");
    description.removeFormat("ulpfec");
    description.removeFormat("flexfec-03");
}

uint32_t allocate_slot_ssrc() noexcept
{
    static std::atomic<uint32_t> next { 0x44520001 };
    uint32_t value = next.fetch_add(1, std::memory_order_relaxed);
    if (value == 0) {
        value = next.fetch_add(1, std::memory_order_relaxed);
    }
    return value;
}

std::optional<uint8_t> mid_extension_id(
    rtc::Description::Media description) noexcept
{
    for (const int id : description.extIds()) {
        const auto* extension = description.extMap(id);
        if (id > 0 && id <= 255 && extension
            && extension->uri == kMidExtensionUri) {
            return static_cast<uint8_t>(id);
        }
    }
    return std::nullopt;
}

RtpFormat primary_rtp_format(const rtc::Description::Media& description,
    std::string_view media_type) noexcept
{
    for (const int payload_type : description.payloadTypes()) {
        const auto* mapping = try_rtp_map(description, payload_type);
        if (!mapping || payload_type < 0 || payload_type > 127) {
            continue;
        }
        const std::string format = lowercase(mapping->format);
        if ((media_type == "audio" && format != "opus")
            || (media_type == "video"
                && is_auxiliary_video_format(format))) {
            continue;
        }
        return { static_cast<uint8_t>(payload_type),
            static_cast<uint32_t>(mapping->clockRate) };
    }
    return media_type == "audio" ? RtpFormat { 111, 48'000 }
                                 : RtpFormat { 96, 90'000 };
}

} // namespace driscord::sfu
