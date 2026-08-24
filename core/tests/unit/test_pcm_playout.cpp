// GoogleWebRtcPcmPlayout: the hardware boundary that carries mixed screen
// system audio from the WebRTC thread to miniaudio's realtime callback.
//
// The realtime render path runs inside a device callback and needs an audio
// device, so it is out of scope for a headless unit test. What is testable
// without hardware — and was entirely untested — is the volume/mute/level
// state surface and the "no device, so push is a safe no-op" contract, which
// is exactly the boundary a caller (GoogleWebRtcClient) drives.

#include "webrtc/google_webrtc_pcm_playout.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

TEST(PcmPlayout, DefaultsAreSaneBeforeStart)
{
    GoogleWebRtcPcmPlayout playout;
    EXPECT_FLOAT_EQ(playout.volume(), 1.0f);
    EXPECT_FALSE(playout.muted());
    EXPECT_FLOAT_EQ(playout.output_level(), 0.0f);
}

TEST(PcmPlayout, VolumeIsClampedToTheSupportedRange)
{
    GoogleWebRtcPcmPlayout playout;

    playout.set_volume(0.5f);
    EXPECT_FLOAT_EQ(playout.volume(), 0.5f);

    // Below zero clamps to silence.
    playout.set_volume(-1.0f);
    EXPECT_FLOAT_EQ(playout.volume(), 0.0f);

    // Above the 2.0 ceiling clamps to the ceiling (200% boost).
    playout.set_volume(10.0f);
    EXPECT_FLOAT_EQ(playout.volume(), 2.0f);

    playout.set_volume(2.0f);
    EXPECT_FLOAT_EQ(playout.volume(), 2.0f);
}

TEST(PcmPlayout, MuteTogglesIndependentlyOfVolume)
{
    GoogleWebRtcPcmPlayout playout;
    playout.set_volume(1.5f);

    playout.set_muted(true);
    EXPECT_TRUE(playout.muted());
    // Muting does not discard the volume setting; it is restored on unmute.
    EXPECT_FLOAT_EQ(playout.volume(), 1.5f);

    playout.set_muted(false);
    EXPECT_FALSE(playout.muted());
    EXPECT_FLOAT_EQ(playout.volume(), 1.5f);
}

TEST(PcmPlayout, PushWithoutStartIsASafeNoOp)
{
    GoogleWebRtcPcmPlayout playout;
    // 10 ms of 48 kHz stereo silence. With no device started the object is not
    // accepting, so this must neither enqueue nor crash.
    std::vector<int16_t> frame(
        GoogleWebRtcPcmPlayout::kSampleRate / 100
            * GoogleWebRtcPcmPlayout::kChannels,
        0);
    for (int i = 0; i < 1000; ++i) {
        playout.push(frame);
    }
    EXPECT_FLOAT_EQ(playout.output_level(), 0.0f);
}

TEST(PcmPlayout, PushRejectsMisalignedFrameCounts)
{
    GoogleWebRtcPcmPlayout playout;
    // An odd sample count cannot be whole stereo frames; push must drop it
    // rather than desynchronize the channels. No device: still a no-op, but
    // this documents the alignment guard as intended behaviour.
    std::array<int16_t, 3> misaligned { 1, 2, 3 };
    playout.push(misaligned);
    playout.push({ }); // empty span
    EXPECT_FLOAT_EQ(playout.output_level(), 0.0f);
}

} // namespace
