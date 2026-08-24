// Fuzzes the SFU's RTP rewrite path — the code that parses attacker-supplied
// RTP headers and mutates SSRC/MID/sequence/timestamp for a subscriber slot.
//
// Shape follows mediasoup's worker/fuzzer RTP target: cheap validity gate,
// then hammer the *mutating* API on the parsed packet and re-run the parser
// over its own output, because an SFU's bugs live in the rewrite, not the
// read.

#include "rtp_slot_rewriter.hpp"
#include "sfu_media_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 2) {
        return 0;
    }
    // First two bytes steer the rewrite parameters; the rest is the packet.
    const uint8_t control = data[0];
    const std::optional<uint8_t> mid_extension_id
        = (control & 0x80) ? std::optional<uint8_t>(data[1] & 0x0f)
                           : std::nullopt;
    const uint32_t output_ssrc = 0x40000000u + control;
    data += 2;
    size -= 2;

    rtc::binary packet(size);
    if (size > 0) {
        std::memcpy(packet.data(), data, size);
    }

    // Stateless rewrite: SSRC + MID blanking.
    rtc::binary stateless = packet;
    (void)driscord::sfu::rewrite_rtp_for_slot(
        stateless, output_ssrc, mid_extension_id);
    // Idempotence probe: the rewriter must accept its own output.
    (void)driscord::sfu::rewrite_rtp_for_slot(
        stateless, output_ssrc + 1, mid_extension_id);

    // Stateful rewrite across a publisher switch: two generations, packets
    // from a stale generation must be rejected, never mangled.
    driscord::sfu::RtpSlotRewriter rewriter(
        90000, 3000 + (control & 0x3f));
    const uint64_t first_generation = rewriter.begin_source();
    rtc::binary stateful = packet;
    (void)rewriter.rewrite(
        stateful, first_generation, output_ssrc, mid_extension_id);
    rewriter.end_source();
    const uint64_t second_generation = rewriter.begin_source();
    rtc::binary switched = packet;
    (void)rewriter.rewrite(
        switched, second_generation, output_ssrc, mid_extension_id);
    // Stale generation after the switch: must be a clean rejection.
    rtc::binary stale = packet;
    (void)rewriter.rewrite(
        stale, first_generation, output_ssrc, mid_extension_id);

    // The deterministic fault injector sits on the same packet path.
    driscord::sfu::RtpFaultConfig faults;
    faults.drop_every_nth = 1 + (control & 0x07);
    faults.reorder_every_nth = 1 + ((control >> 3) & 0x07);
    driscord::sfu::RtpFaultState state;
    auto result
        = driscord::sfu::apply_rtp_faults(faults, state, std::move(packet));
    if (result.first) {
        (void)driscord::sfu::rewrite_rtp_for_slot(
            *result.first, output_ssrc, mid_extension_id);
    }
    return 0;
}
