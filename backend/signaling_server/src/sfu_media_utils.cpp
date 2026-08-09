#include "sfu_media_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
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

} // namespace

RtpFaultResult apply_rtp_faults(const RtpFaultConfig& config,
    RtpFaultState& state,
    rtc::binary packet)
{
    if (rtc::IsRtcp(packet)
        || (config.drop_every_nth == 0
            && config.reorder_every_nth == 0)) {
        return { .first = std::move(packet), .second = std::nullopt };
    }

    ++state.packets_seen;
    if (config.drop_every_nth != 0
        && state.packets_seen % config.drop_every_nth == 0) {
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
        if (auto* mapping = description.rtpMap(payload_type)) {
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
        const auto* mapping = description.rtpMap(payload_type);
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
