#include "frame_marker.hpp"
#include "screen_content.hpp"
#include "video_quality.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;
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

test_util::I420Frame marked_frame(uint32_t index)
{
    auto bgra = test_util::scrolling_text_frame(kWidth, kHeight, index);
    test_util::encode_frame_marker(bgra, kWidth, kHeight, kWidth * 4, index);
    return test_util::bgra_to_i420(bgra, kWidth, kHeight, kWidth * 4);
}

TEST(VideoQualityAccumulator, IdentityPlaybackScoresCleanly)
{
    test_util::ReferenceFrameStore references(200);
    const auto base = std::chrono::steady_clock::now();
    std::vector<test_util::I420Frame> frames;
    for (uint32_t i = 0; i < 60; ++i) {
        auto frame = marked_frame(i);
        references.add(i, frame, base + i * 33'333us);
        frames.push_back(std::move(frame));
    }

    test_util::VideoQualityAccumulator accumulator(references, kWidth,
        kHeight);
    for (uint32_t i = 0; i < 60; ++i) {
        accumulator.on_decoded(view_of(frames[i]),
            base + i * 33'333us + 40ms);
    }
    const auto report = accumulator.report();
    EXPECT_EQ(report.compared_frames, 60u);
    EXPECT_EQ(report.undecodable_markers, 0u);
    EXPECT_DOUBLE_EQ(report.psnr_mean, test_util::kPsnrCapDb);
    EXPECT_GT(report.ssim_min, 0.999);
    EXPECT_EQ(report.dropped_frames, 0u);
    ASSERT_EQ(report.e2e_delay_ms.size(), 60u);
    EXPECT_NEAR(report.e2e_delay_ms.front(), 40.0, 0.5);
}

TEST(VideoQualityAccumulator, CountsDroppedIndexSpan)
{
    test_util::ReferenceFrameStore references(200);
    const auto base = std::chrono::steady_clock::now();
    std::vector<test_util::I420Frame> frames;
    for (uint32_t i = 0; i < 90; ++i) {
        auto frame = marked_frame(i);
        references.add(i, frame, base + i * 33'333us);
        frames.push_back(std::move(frame));
    }

    test_util::VideoQualityAccumulator accumulator(references, kWidth,
        kHeight);
    for (uint32_t i = 0; i < 90; ++i) {
        if (i >= 30 && i < 40) {
            continue;
        }
        accumulator.on_decoded(view_of(frames[i]), base + i * 33'333us);
    }
    const auto report = accumulator.report();
    EXPECT_EQ(report.compared_frames, 80u);
    EXPECT_EQ(report.dropped_frames, 10u);
}

TEST(VideoQualityAccumulator, DumpsComparedPairsAsAlignedY4m)
{
    const auto dir = std::filesystem::temp_directory_path()
        / "driscord_video_quality_dump_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    test_util::ReferenceFrameStore references(10);
    const auto base = std::chrono::steady_clock::now();
    {
        test_util::VideoQualityAccumulator accumulator(references, kWidth,
            kHeight);
        accumulator.enable_dump(dir, "pairs");
        for (uint32_t i = 0; i < 3; ++i) {
            auto frame = marked_frame(i);
            references.add(i, frame, base + i * 33'333us);
            accumulator.on_decoded(view_of(frame),
                base + i * 33'333us + 40ms);
        }
    }

    const size_t frame_bytes
        = 6 + static_cast<size_t>(kWidth) * kHeight * 3 / 2;
    for (const char* name : { "pairs.ref.y4m", "pairs.recv.y4m" }) {
        const auto path = dir / name;
        ASSERT_TRUE(std::filesystem::exists(path)) << name;
        const auto size = std::filesystem::file_size(path);
        ASSERT_GT(size, 3 * frame_bytes) << name;
        EXPECT_LT(size - 3 * frame_bytes, 128u) << name;
        std::ifstream stream(path, std::ios::binary);
        std::string magic(9, '\0');
        stream.read(magic.data(), 9);
        EXPECT_EQ(magic, "YUV4MPEG2") << name;
    }
    EXPECT_EQ(std::filesystem::file_size(dir / "pairs.ref.y4m"),
        std::filesystem::file_size(dir / "pairs.recv.y4m"));
    std::filesystem::remove_all(dir);
}

TEST(VideoQualityAccumulator, DegradedPixelsLowerPsnrNotIdentity)
{
    test_util::ReferenceFrameStore references(10);
    const auto base = std::chrono::steady_clock::now();
    auto pristine = marked_frame(3);
    references.add(3, pristine, base);

    auto degraded = pristine;
    for (size_t i = kWidth * 70; i + 1 < degraded.y.size(); i += 2) {
        degraded.y[i] = static_cast<uint8_t>(
            (degraded.y[i] + degraded.y[i + 1]) / 2);
        degraded.y[i + 1] = degraded.y[i];
    }
    test_util::VideoQualityAccumulator accumulator(references, kWidth,
        kHeight);
    accumulator.on_decoded(view_of(degraded), base + 50ms);
    const auto report = accumulator.report();
    EXPECT_EQ(report.compared_frames, 1u);
    EXPECT_LT(report.psnr_mean, test_util::kPsnrCapDb);
    EXPECT_GT(report.psnr_mean, 20.0);
    EXPECT_LT(report.ssim_mean, 1.0);
}

}
