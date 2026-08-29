#include "audio_probe.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace std::chrono_literals;

constexpr int kRate = 48'000;
constexpr size_t kFrame = 480;

std::vector<int16_t> tone_stream(size_t samples, double frequency_hz)
{
    constexpr double kPi = 3.14159265358979323846;
    std::vector<int16_t> result(samples);
    for (size_t i = 0; i < samples; ++i) {
        result[i] = static_cast<int16_t>(std::lround(
            8'000.0 * std::sin(2.0 * kPi * frequency_hz * static_cast<double>(i) / kRate)));
    }
    return result;
}

std::vector<std::chrono::steady_clock::time_point> run_detector(
    const std::vector<int16_t>& stream,
    std::chrono::steady_clock::time_point base,
    double threshold = 0.5)
{
    test_util::ChirpDetector detector(test_util::make_chirp(), kRate,
        threshold);
    for (size_t frame = 0; frame * kFrame + kFrame <= stream.size(); ++frame) {
        detector.push(
            std::span<const int16_t>(stream.data() + frame * kFrame, kFrame),
            1,
            base + std::chrono::milliseconds(frame * 10));
    }
    return detector.detections();
}

TEST(ChirpDetector, LocatesTheChirpStartOnTheSyntheticClock)
{
    auto stream = tone_stream(kRate * 2, 440.0);
    const size_t chirp_offset = kRate * 7 / 10;
    const auto chirp = test_util::make_chirp();
    test_util::mix_chirp(stream, chirp, chirp_offset);

    const auto base = std::chrono::steady_clock::now();
    const auto detections = run_detector(stream, base);
    ASSERT_EQ(detections.size(), 1u);
    const double offset_ms
        = std::chrono::duration_cast<std::chrono::microseconds>(
              detections.front() - base)
              .count()
        / 1'000.0;
    EXPECT_NEAR(offset_ms, 700.0, 5.0);
}

TEST(ChirpDetector, SurvivesLowpassSmoothing)
{
    auto stream = tone_stream(kRate, 440.0);
    test_util::mix_chirp(stream, test_util::make_chirp(), kRate / 2);
    std::vector<int16_t> filtered(stream.size());
    for (size_t i = 1; i + 1 < stream.size(); ++i) {
        filtered[i] = static_cast<int16_t>(
            (stream[i - 1] + stream[i] + stream[i + 1]) / 3);
    }
    const auto detections
        = run_detector(filtered, std::chrono::steady_clock::now());
    EXPECT_EQ(detections.size(), 1u);
}

TEST(ChirpDetector, NoFalsePositivesOnAPureTone)
{
    const auto stream = tone_stream(kRate * 2, 440.0);
    const auto detections
        = run_detector(stream, std::chrono::steady_clock::now());
    EXPECT_TRUE(detections.empty());
}

TEST(ChirpDetector, SeparatesTwoChirpsHalfASecondApart)
{
    auto stream = tone_stream(kRate * 2, 440.0);
    const auto chirp = test_util::make_chirp();
    test_util::mix_chirp(stream, chirp, kRate / 2);
    test_util::mix_chirp(stream, chirp, kRate);
    const auto base = std::chrono::steady_clock::now();
    const auto detections = run_detector(stream, base);
    ASSERT_EQ(detections.size(), 2u);
    const double gap_ms
        = std::chrono::duration_cast<std::chrono::microseconds>(
              detections[1] - detections[0])
              .count()
        / 1'000.0;
    EXPECT_NEAR(gap_ms, 500.0, 5.0);
}

}
