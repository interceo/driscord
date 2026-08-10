#include "sfu_media_utils.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr std::string_view kMidUri = "urn:ietf:params:rtp-hdrext:sdes:mid";
constexpr std::string_view kTwccUri = "http://www.ietf.org/id/"
                                      "draft-holmer-rmcat-transport-wide-cc-extensions-01";

rtc::Description::Media opus_audio()
{
    rtc::Description::Media audio("audio 9 UDP/TLS/RTP/SAVPF 111", "voice");
    rtc::Description::Media::RtpMap opus("111 opus/48000/2");
    opus.addFeedback("transport-cc");
    opus.addFeedback("nack");
    audio.addRtpMap(opus);
    return audio;
}

rtc::Description::Media h264_video()
{
    rtc::Description::Media video(
        "video 9 UDP/TLS/RTP/SAVPF 96 97 98", "screen");
    rtc::Description::Media::RtpMap h264("96 H264/90000");
    h264.addFeedback("transport-cc");
    h264.addFeedback("goog-remb");
    h264.addFeedback("nack");
    h264.addFeedback("nack pli");
    video.addRtpMap(h264);
    video.addRtpMap(rtc::Description::Media::RtpMap("97 rtx/90000"));
    video.addRtpMap(rtc::Description::Media::RtpMap("98 red/90000"));
    return video;
}

std::vector<std::string> feedback_of(
    const rtc::Description::Media& description, int payload_type)
{
    const auto* mapping = description.rtpMap(payload_type);
    return mapping ? mapping->rtcpFbs : std::vector<std::string> { };
}

bool has_feedback(const rtc::Description::Media& description,
    int payload_type,
    std::string_view name)
{
    const auto feedback = feedback_of(description, payload_type);
    return std::find(feedback.begin(), feedback.end(), name) != feedback.end();
}

} // namespace

// A payload type listed in the m= line but never described by a=rtpmap is
// legal SDP. libdatachannel throws when asked for the missing map, so every
// lookup here has to tolerate it — the noexcept helpers used to abort the whole
// server on a remote peer's offer.
TEST(SfuMediaUtils, ToleratesPayloadTypesWithoutRtpMap)
{
    rtc::Description::Media audio("audio 9 UDP/TLS/RTP/SAVPF 0 111", "voice");
    audio.addRtpMap(rtc::Description::Media::RtpMap("111 opus/48000/2"));

    EXPECT_NO_THROW(driscord::sfu::apply_forwarding_feedback_policy(audio));
    const auto format = driscord::sfu::primary_rtp_format(audio, "audio");
    EXPECT_EQ(format.payload_type, 111);
    EXPECT_EQ(format.clock_rate, 48'000u);
}

TEST(SfuMediaUtils, FallsBackWhenNoPayloadTypeIsMapped)
{
    rtc::Description::Media audio("audio 9 UDP/TLS/RTP/SAVPF 0", "voice");

    EXPECT_NO_THROW(driscord::sfu::apply_forwarding_feedback_policy(audio));
    const auto audio_format = driscord::sfu::primary_rtp_format(audio, "audio");
    EXPECT_EQ(audio_format.payload_type, 111);
    EXPECT_EQ(audio_format.clock_rate, 48'000u);

    rtc::Description::Media video("video 9 UDP/TLS/RTP/SAVPF 96", "screen");
    const auto video_format = driscord::sfu::primary_rtp_format(video, "video");
    EXPECT_EQ(video_format.payload_type, 96);
    EXPECT_EQ(video_format.clock_rate, 90'000u);
}

TEST(SfuMediaUtils, PrimaryVideoFormatSkipsRetransmissionAndFec)
{
    const auto video = h264_video();
    const auto format = driscord::sfu::primary_rtp_format(video, "video");
    EXPECT_EQ(format.payload_type, 96);
    EXPECT_EQ(format.clock_rate, 90'000u);
}

TEST(SfuMediaUtils, ForwardingPolicyDropsUnproxyableFeedbackOnly)
{
    auto video = h264_video();
    driscord::sfu::apply_forwarding_feedback_policy(video);

    EXPECT_FALSE(has_feedback(video, 96, "transport-cc"));
    EXPECT_FALSE(has_feedback(video, 96, "goog-remb"));
    // Hop-local repair stays: NACK and PLI are answered by the SFU itself.
    EXPECT_TRUE(has_feedback(video, 96, "nack"));
    EXPECT_TRUE(has_feedback(video, 96, "nack pli"));
}

TEST(SfuMediaUtils, ForwardingPolicyRemovesTransportWideCcExtension)
{
    auto audio = opus_audio();
    audio.addExtMap(rtc::Description::Media::ExtMap(1, std::string(kMidUri)));
    audio.addExtMap(rtc::Description::Media::ExtMap(3, std::string(kTwccUri)));

    driscord::sfu::apply_forwarding_feedback_policy(audio);

    const auto ids = audio.extIds();
    EXPECT_NE(std::find(ids.begin(), ids.end(), 1), ids.end());
    EXPECT_EQ(std::find(ids.begin(), ids.end(), 3), ids.end());
    EXPECT_FALSE(has_feedback(audio, 111, "transport-cc"));
    EXPECT_TRUE(has_feedback(audio, 111, "nack"));
}

// The mid extension id is what the rewriter uses to blank the publisher's mid
// out of forwarded packets; losing it would leak the wrong mid downstream.
TEST(SfuMediaUtils, FindsTheMidExtensionId)
{
    auto audio = opus_audio();
    EXPECT_FALSE(driscord::sfu::mid_extension_id(audio).has_value());

    audio.addExtMap(rtc::Description::Media::ExtMap(4, std::string(kMidUri)));
    const auto id = driscord::sfu::mid_extension_id(audio);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(*id, 4);
}

TEST(SfuMediaUtils, RemovesAuxiliaryVideoCodecs)
{
    auto video = h264_video();
    driscord::sfu::remove_auxiliary_video_codecs(video);

    const auto types = video.payloadTypes();
    EXPECT_NE(std::find(types.begin(), types.end(), 96), types.end());
    EXPECT_EQ(std::find(types.begin(), types.end(), 97), types.end());
    EXPECT_EQ(std::find(types.begin(), types.end(), 98), types.end());
}

TEST(SfuMediaUtils, SlotSsrcsAreUniqueAndNonZero)
{
    std::unordered_set<uint32_t> seen;
    for (int i = 0; i < 512; ++i) {
        const uint32_t ssrc = driscord::sfu::allocate_slot_ssrc();
        EXPECT_NE(ssrc, 0u);
        EXPECT_TRUE(seen.insert(ssrc).second);
    }
}
