#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

namespace test_util {

[[nodiscard]] std::vector<int16_t> make_chirp(int sample_rate_hz = 48'000,
    int duration_ms = 20,
    double start_hz = 1'000.0,
    double end_hz = 4'000.0,
    int amplitude = 14'000);

void mix_chirp(std::span<int16_t> destination,
    std::span<const int16_t> chirp,
    size_t sample_offset = 0);

class ChirpDetector {
public:
    explicit ChirpDetector(std::vector<int16_t> chirp_template,
        int sample_rate_hz = 48'000,
        double threshold = 0.5);

    void push(std::span<const int16_t> samples,
        size_t channels,
        std::chrono::steady_clock::time_point entry_time);

    [[nodiscard]] std::vector<std::chrono::steady_clock::time_point>
    detections() const;

private:
    struct Template {
        std::vector<float> samples;
        double norm = 0.0;
    };

    std::vector<Template> templates_;
    size_t longest_template_ = 0;
    int sample_rate_hz_;
    double threshold_;
    std::vector<float> buffer_;
    uint64_t base_index_ = 0;
    uint64_t scanned_until_ = 0;
    uint64_t last_detection_index_ = 0;
    bool has_detection_ = false;
    std::vector<std::chrono::steady_clock::time_point> detections_;
};

}
