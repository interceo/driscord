#include "frame_marker.hpp"
#include "screen_content.hpp"
#include "video_quality.hpp"

#include "libyuv/scale.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using driscord::media::DecodedVideoFrameView;

constexpr int kWidth = 320;
constexpr int kHeight = 180;

DecodedVideoFrameView view_of(const test_util::I420Frame& frame)
{
    DecodedVideoFrameView view;
    view.y = frame.y.data();
    view.u = frame.u.data();
    view.v = frame.v.data();
    view.y_stride = frame.y_stride();
    view.u_stride = frame.chroma_stride();
    view.v_stride = frame.chroma_stride();
    view.width = frame.width;
    view.height = frame.height;
    return view;
}

test_util::I420Frame marked_frame(uint32_t frame_index)
{
    auto bgra = test_util::scrolling_text_frame(kWidth, kHeight, frame_index);
    test_util::encode_frame_marker(bgra, kWidth, kHeight, kWidth * 4,
        frame_index);
    return test_util::bgra_to_i420(bgra, kWidth, kHeight, kWidth * 4);
}

TEST(FrameMarker, RoundTripsThroughI420)
{
    for (const uint32_t index : { 0u, 1u, 255u, 12'345u, 65'535u }) {
        const auto frame = marked_frame(index);
        const auto marker
            = test_util::decode_frame_marker(view_of(frame), kWidth, kHeight);
        ASSERT_TRUE(marker.has_value()) << "index " << index;
        EXPECT_EQ(marker->frame_index, index);
    }
}

TEST(FrameMarker, SurvivesAHalfResolutionDownscale)
{
    const uint32_t index = 4'242;
    const auto full = marked_frame(index);

    test_util::I420Frame half;
    half.width = kWidth / 2;
    half.height = kHeight / 2;
    half.y.resize(static_cast<size_t>(half.width) * half.height);
    const size_t chroma = static_cast<size_t>((half.width + 1) / 2)
        * static_cast<size_t>((half.height + 1) / 2);
    half.u.resize(chroma);
    half.v.resize(chroma);
    libyuv::I420Scale(full.y.data(), full.y_stride(),
        full.u.data(), full.chroma_stride(),
        full.v.data(), full.chroma_stride(),
        full.width, full.height,
        half.y.data(), half.y_stride(),
        half.u.data(), half.chroma_stride(),
        half.v.data(), half.chroma_stride(),
        half.width, half.height, libyuv::kFilterBilinear);

    const auto marker
        = test_util::decode_frame_marker(view_of(half), kWidth, kHeight);
    ASSERT_TRUE(marker.has_value());
    EXPECT_EQ(marker->frame_index, index);
}

TEST(FrameMarker, RejectsCorruptedPayload)
{
    auto bgra = test_util::scrolling_text_frame(kWidth, kHeight, 7);
    test_util::encode_frame_marker(bgra, kWidth, kHeight, kWidth * 4, 7);
    // Overwrite two cells in the middle of the grid with mid-gray: the
    // checksum must catch it rather than return a wrong index.
    for (int y = test_util::kMarkerMarginPx;
        y < test_util::kMarkerMarginPx + test_util::kMarkerCellPx; ++y) {
        for (int x = test_util::kMarkerMarginPx;
            x < test_util::kMarkerMarginPx + 2 * test_util::kMarkerCellPx;
            ++x) {
            uint8_t* pixel = bgra.data()
                + (static_cast<size_t>(y) * kWidth + static_cast<size_t>(x))
                    * 4;
            pixel[0] = 128;
            pixel[1] = 128;
            pixel[2] = 128;
        }
    }
    const auto frame = test_util::bgra_to_i420(bgra, kWidth, kHeight,
        kWidth * 4);
    const auto marker
        = test_util::decode_frame_marker(view_of(frame), kWidth, kHeight);
    if (marker.has_value()) {
        // Mid-gray lands on the threshold boundary; the only acceptable
        // decode is the correct one.
        EXPECT_EQ(marker->frame_index, 7u);
    }
}

TEST(FrameMarker, IdentityComparisonHitsThePsnrCap)
{
    const auto frame = marked_frame(99);
    EXPECT_DOUBLE_EQ(
        test_util::i420_psnr(frame, view_of(frame)), test_util::kPsnrCapDb);
    EXPECT_GT(test_util::i420_ssim(frame, view_of(frame)), 0.999);
}

TEST(FrameMarker, TooSmallFramesAreLeftUntouched)
{
    std::vector<uint8_t> tiny(64 * 32 * 4, 200);
    const auto before = tiny;
    test_util::encode_frame_marker(tiny, 64, 32, 64 * 4, 5);
    EXPECT_EQ(tiny, before);
}

} // namespace
