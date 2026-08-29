#include "audio_probe.hpp"

#include <algorithm>
#include <cmath>

namespace test_util {

std::vector<int16_t> make_chirp(int sample_rate_hz,
    int duration_ms,
    double start_hz,
    double end_hz,
    int amplitude)
{
    constexpr double kPi = 3.14159265358979323846;
    const size_t samples = static_cast<size_t>(sample_rate_hz)
        * static_cast<size_t>(duration_ms) / 1'000;
    std::vector<int16_t> result(samples);
    const double duration_s = static_cast<double>(duration_ms) / 1'000.0;
    const double sweep = (end_hz - start_hz) / duration_s;
    for (size_t i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / sample_rate_hz;
        const double phase
            = 2.0 * kPi * (start_hz * t + 0.5 * sweep * t * t);
        const double window = 0.5
            * (1.0
                - std::cos(2.0 * kPi * static_cast<double>(i)
                    / static_cast<double>(samples - 1)));
        result[i] = static_cast<int16_t>(
            std::lround(amplitude * window * std::sin(phase)));
    }
    return result;
}

void mix_chirp(std::span<int16_t> destination,
    std::span<const int16_t> chirp,
    size_t sample_offset)
{
    for (size_t i = 0;
        i < chirp.size() && sample_offset + i < destination.size(); ++i) {
        const int mixed = static_cast<int>(destination[sample_offset + i])
            + static_cast<int>(chirp[i]);
        destination[sample_offset + i] = static_cast<int16_t>(
            std::clamp(mixed, -32'768, 32'767));
    }
}

namespace {

    std::vector<float> stretch_template(const std::vector<int16_t>& base,
        double factor)
    {
        const auto length
            = static_cast<size_t>(static_cast<double>(base.size()) * factor);
        std::vector<float> result(length);
        for (size_t i = 0; i < length; ++i) {
            const double source = static_cast<double>(i) / factor;
            const auto low = static_cast<size_t>(source);
            const size_t high = std::min(low + 1, base.size() - 1);
            const double weight = source - static_cast<double>(low);
            result[i] = static_cast<float>(
                (1.0 - weight) * base[low] + weight * base[high]);
        }
        return result;
    }

}

ChirpDetector::ChirpDetector(std::vector<int16_t> chirp_template,
    int sample_rate_hz,
    double threshold)
    : sample_rate_hz_(sample_rate_hz)
    , threshold_(threshold)
{
    for (const double factor : { 0.94, 1.0, 1.06 }) {
        Template variant;
        variant.samples = stretch_template(chirp_template, factor);
        double norm = 0.0;
        for (const float sample : variant.samples) {
            norm += static_cast<double>(sample) * sample;
        }
        variant.norm = std::sqrt(norm);
        longest_template_
            = std::max(longest_template_, variant.samples.size());
        templates_.push_back(std::move(variant));
    }
}

void ChirpDetector::push(std::span<const int16_t> samples,
    size_t channels,
    std::chrono::steady_clock::time_point entry_time)
{
    if (channels == 0 || templates_.empty()) {
        return;
    }
    size_t frame_samples = 0;
    for (size_t i = 0; i < samples.size(); i += channels) {
        buffer_.push_back(static_cast<float>(samples[i]));
        ++frame_samples;
    }
    const uint64_t total_samples = base_index_ + buffer_.size();
    const uint64_t frame_start_index = total_samples - frame_samples;

    while (scanned_until_ + longest_template_ <= total_samples) {
        const size_t local = static_cast<size_t>(scanned_until_ - base_index_);
        double correlation = 0.0;
        for (const auto& variant : templates_) {
            double dot = 0.0;
            double energy = 0.0;
            for (size_t i = 0; i < variant.samples.size(); ++i) {
                const double sample = buffer_[local + i];
                dot += sample * variant.samples[i];
                energy += sample * sample;
            }
            const double denominator = variant.norm * std::sqrt(energy);
            if (denominator > 1e-6) {
                correlation = std::max(correlation, dot / denominator);
            }
        }
        if (correlation >= threshold_) {
            const uint64_t refractory
                = static_cast<uint64_t>(sample_rate_hz_) / 10;
            if (!has_detection_
                || scanned_until_ >= last_detection_index_ + refractory) {
                const double seconds_before_frame
                    = (static_cast<double>(frame_start_index)
                          - static_cast<double>(scanned_until_))
                    / sample_rate_hz_;
                detections_.push_back(entry_time
                    - std::chrono::microseconds(static_cast<int64_t>(
                        seconds_before_frame * 1e6)));
                last_detection_index_ = scanned_until_;
                has_detection_ = true;
            }
        }
        ++scanned_until_;
    }

    const size_t max_samples = static_cast<size_t>(sample_rate_hz_);
    if (buffer_.size() > max_samples) {
        const size_t trim = buffer_.size() - max_samples;
        buffer_.erase(buffer_.begin(),
            buffer_.begin() + static_cast<ptrdiff_t>(trim));
        base_index_ += trim;
    }
}

std::vector<std::chrono::steady_clock::time_point>
ChirpDetector::detections() const
{
    return detections_;
}

}
