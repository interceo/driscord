#pragma once

// Audio ground-truth markers: a short windowed linear chirp with a sharp
// autocorrelation peak that survives Opus, plus a matched-filter detector
// fed from the post-NetEq playout tap (on_rendered_audio). Detection times
// use the callback entry wall clock — the injected TestAudioDeviceModule
// paces playout in real time and DecodedAudioFrameView carries no timestamp.

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

namespace test_util {

// 20 ms 1->4 kHz Hann-windowed linear chirp at 48 kHz by default.
[[nodiscard]] std::vector<int16_t> make_chirp(int sample_rate_hz = 48'000,
    int duration_ms = 20,
    double start_hz = 1'000.0,
    double end_hz = 4'000.0,
    int amplitude = 14'000);

// Saturating mix of the chirp into a PCM buffer at sample_offset.
void mix_chirp(std::span<int16_t> destination,
    std::span<const int16_t> chirp,
    size_t sample_offset = 0);

// Sliding normalized cross-correlation against the chirp template over a
// mono playout stream. NetEq's accelerate/preemptive time-stretching warps
// the played-out chirp, so the filter also matches slightly compressed and
// dilated template variants and takes the best score. Feed every rendered
// 10 ms frame; detections are the wall-clock instants the chirp STARTED
// playing, deduplicated within a refractory window.
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
    // Absolute index of buffer_[0] since the stream started.
    uint64_t base_index_ = 0;
    uint64_t scanned_until_ = 0;
    uint64_t last_detection_index_ = 0;
    bool has_detection_ = false;
    std::vector<std::chrono::steady_clock::time_point> detections_;
};

} // namespace test_util
