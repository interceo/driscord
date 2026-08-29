
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

    playout.set_volume(-1.0f);
    EXPECT_FLOAT_EQ(playout.volume(), 0.0f);

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
    EXPECT_FLOAT_EQ(playout.volume(), 1.5f);

    playout.set_muted(false);
    EXPECT_FALSE(playout.muted());
    EXPECT_FLOAT_EQ(playout.volume(), 1.5f);
}

TEST(PcmPlayout, PushWithoutStartIsASafeNoOp)
{
    GoogleWebRtcPcmPlayout playout;
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
    std::array<int16_t, 3> misaligned { 1, 2, 3 };
    playout.push(misaligned);
    playout.push({ });
    EXPECT_FLOAT_EQ(playout.output_level(), 0.0f);
}

}
