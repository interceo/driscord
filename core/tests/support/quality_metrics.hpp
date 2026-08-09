#pragma once

#include "audio/audio.hpp"
#include "video_quality.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace test_util {

// --- helpers -------------------------------------------------------------

inline double percentile_of(std::vector<double> v, int percent)
{
    if (v.empty()) {
        return 0.0;
    }
    const size_t rank = std::min(v.size() - 1,
        v.size() * static_cast<size_t>(percent) / 100);
    auto nth = v.begin() + static_cast<ptrdiff_t>(rank);
    std::nth_element(v.begin(), nth, v.end());
    return *nth;
}

inline int64_t percentile_of(std::vector<int64_t> v, int percent)
{
    if (v.empty()) {
        return 0;
    }
    const size_t rank = std::min(v.size() - 1,
        v.size() * static_cast<size_t>(percent) / 100);
    auto nth = v.begin() + static_cast<ptrdiff_t>(rank);
    std::nth_element(v.begin(), nth, v.end());
    return *nth;
}

// --- video ---------------------------------------------------------------

struct VideoMetrics {
    double psnr_avg_db = 0.0;
    double psnr_p05_db = 0.0;
    double ssim_avg = 0.0;
    double ssim_p05 = 0.0;
    size_t frames_captured = 0;
    size_t frames_rendered = 0;
    size_t frames_matched = 0;
    double dropped_ratio = 0.0; // captured but never rendered
    size_t freeze_count = 0;
    int64_t freeze_total_ms = 0;
    int64_t max_freeze_ms = 0;
    double harmonic_fps = 0.0;
    int64_t p95_inter_frame_ms = 0;
    int64_t time_to_first_frame_ms = -1;
    int64_t transport_p50_ms = 0;
    int64_t transport_p95_ms = 0;
};

// Matches rendered frames back to captured ones by frame id — the same trick
// WebRTC's DefaultVideoQualityAnalyzer uses, and free here because frame_id is
// already on the wire in FrameHeader.
class VideoQualityAnalyzer {
public:
    using ReferenceFn = std::function<const std::vector<uint8_t>&(uint64_t frame_id)>;

    VideoQualityAnalyzer(int width, int height, int fps, ReferenceFn reference)
        : cmp_(width, height)
        , nominal_us_(1'000'000 / std::max(1, fps))
        , reference_(std::move(reference))
    {
    }

    bool ok() const noexcept { return cmp_.ok(); }
    const std::string& error() const noexcept { return cmp_.error(); }

    void on_captured(uint64_t frame_id, int64_t capture_ts_us)
    {
        captured_.push_back({ frame_id, capture_ts_us });
    }

    void on_rendered(uint64_t frame_id, const uint8_t* rgba, int64_t render_ts_us)
    {
        if (rendered_ids_.count(frame_id)) {
            return; // the renderer holds the last frame; only the first show counts
        }
        rendered_ids_.insert(frame_id);

        if (first_render_us_ < 0) {
            first_render_us_ = render_ts_us;
        } else {
            const int64_t gap = render_ts_us - last_render_us_;
            inter_frame_ms_.push_back(gap / 1000);
            inv_fps_sum_ += static_cast<double>(gap) / 1'000'000.0;
            if (gap > 3 * nominal_us_) {
                ++freezes_;
                freeze_total_us_ += gap;
                max_freeze_us_ = std::max(max_freeze_us_, gap);
            }
        }
        last_render_us_ = render_ts_us;

        for (const auto& c : captured_) {
            if (c.frame_id == frame_id) {
                transport_ms_.push_back((render_ts_us - c.capture_ts_us) / 1000);
                break;
            }
        }
        cmp_.add_pair(reference_(frame_id).data(), rgba);
    }

    VideoMetrics finish(int64_t session_start_us)
    {
        const auto q = cmp_.finish();
        VideoMetrics m;
        m.psnr_avg_db = q.psnr_avg_db;
        m.psnr_p05_db = q.psnr_p05_db;
        m.ssim_avg = q.ssim_avg;
        m.ssim_p05 = q.ssim_p05;
        m.frames_matched = q.frames;
        m.frames_captured = captured_.size();
        m.frames_rendered = rendered_ids_.size();
        m.dropped_ratio = captured_.empty()
            ? 0.0
            : 1.0 - static_cast<double>(rendered_ids_.size())
                / static_cast<double>(captured_.size());
        m.freeze_count = freezes_;
        m.freeze_total_ms = freeze_total_us_ / 1000;
        m.max_freeze_ms = max_freeze_us_ / 1000;
        // Harmonic mean: one long stall drags this down the way it drags the
        // viewing experience down, which an arithmetic mean hides.
        m.harmonic_fps = inv_fps_sum_ > 0.0
            ? static_cast<double>(inter_frame_ms_.size()) / inv_fps_sum_
            : 0.0;
        m.p95_inter_frame_ms = percentile_of(inter_frame_ms_, 95);
        m.time_to_first_frame_ms
            = first_render_us_ < 0 ? -1 : (first_render_us_ - session_start_us) / 1000;
        m.transport_p50_ms = percentile_of(transport_ms_, 50);
        m.transport_p95_ms = percentile_of(transport_ms_, 95);
        return m;
    }

private:
    struct Captured {
        uint64_t frame_id;
        int64_t capture_ts_us;
    };

    FrameComparator cmp_;
    int64_t nominal_us_;
    ReferenceFn reference_;

    std::vector<Captured> captured_;
    std::set<uint64_t> rendered_ids_;
    std::vector<int64_t> inter_frame_ms_;
    std::vector<int64_t> transport_ms_;
    double inv_fps_sum_ = 0.0;
    int64_t first_render_us_ = -1;
    int64_t last_render_us_ = 0;
    size_t freezes_ = 0;
    int64_t freeze_total_us_ = 0;
    int64_t max_freeze_us_ = 0;
};

// --- audio ---------------------------------------------------------------

struct AudioMetrics {
    double seg_snr_db = 0.0;
    double seg_snr_p05_db = 0.0;
    size_t glitch_count = 0;
    double silence_ratio = 0.0;
    // NetEq's vocabulary, as fractions of emitted samples.
    double expand_rate = 0.0;
    double accelerate_rate = 0.0;
    double preemptive_rate = 0.0;
    double fec_rate = 0.0;
    double underrun_rate = 0.0;
    uint64_t samples_out = 0;
};

// Compares the played-out signal against the reference that was encoded.
//
// The playout offset is not known in advance and WSOLA moves it during the
// call, so alignment is searched per window rather than assumed once.
class AudioQualityAnalyzer {
public:
    AudioQualityAnalyzer(std::vector<float> reference, int sample_rate)
        : ref_(std::move(reference))
        , rate_(sample_rate)
    {
    }

    void on_played(const float* pcm, size_t frames)
    {
        out_.insert(out_.end(), pcm, pcm + frames);
    }

    AudioMetrics finish(const AudioReceiver::Stats& s)
    {
        AudioMetrics m;
        m.samples_out = s.total_samples_out;
        if (s.total_samples_out > 0) {
            const auto denom = static_cast<double>(s.total_samples_out);
            m.expand_rate = static_cast<double>(s.conceal_samples) / denom;
            m.fec_rate = static_cast<double>(s.fec_samples) / denom;
            m.underrun_rate = static_cast<double>(s.silence_samples) / denom;
            // Time-scale corrections split by direction: shorter output than
            // input is acceleration, longer is preemptive expansion.
            if (s.stretch_out_samples < s.stretch_in_samples) {
                m.accelerate_rate
                    = static_cast<double>(s.stretch_in_samples - s.stretch_out_samples) / denom;
            } else {
                m.preemptive_rate
                    = static_cast<double>(s.stretch_out_samples - s.stretch_in_samples) / denom;
            }
        }

        m.glitch_count = count_glitches();
        m.silence_ratio = silence_ratio();

        std::vector<double> snrs;
        const size_t win = static_cast<size_t>(rate_) / 50; // 20 ms
        const size_t search = static_cast<size_t>(rate_) / 200; // +/- 5 ms
        int64_t lag = coarse_lag();
        for (size_t o = 0; o + win + search < out_.size(); o += win) {
            double best = -1e300;
            int64_t best_lag = lag;
            for (int64_t d = -static_cast<int64_t>(search);
                d <= static_cast<int64_t>(search); d += 8) {
                const int64_t r = static_cast<int64_t>(o) + lag + d;
                if (r < 0 || r + static_cast<int64_t>(win) >= static_cast<int64_t>(ref_.size())) {
                    continue;
                }
                const double c = correlate(o, static_cast<size_t>(r), win);
                if (c > best) {
                    best = c;
                    best_lag = lag + d;
                }
            }
            const int64_t r = static_cast<int64_t>(o) + best_lag;
            if (r < 0 || r + static_cast<int64_t>(win) >= static_cast<int64_t>(ref_.size())) {
                continue;
            }
            lag = best_lag;
            if (const double snr = segment_snr(o, static_cast<size_t>(r), win);
                !std::isnan(snr)) {
                snrs.push_back(snr);
            }
        }
        if (!snrs.empty()) {
            double sum = 0.0;
            for (double x : snrs) {
                sum += x;
            }
            m.seg_snr_db = sum / static_cast<double>(snrs.size());
            m.seg_snr_p05_db = percentile_of(snrs, 5);
        }
        return m;
    }

private:
    // A step between neighbouring samples far above the local level is a splice
    // artefact, not signal — the same discontinuity check test_wsola uses.
    size_t count_glitches() const
    {
        const size_t win = static_cast<size_t>(rate_) / 100; // 10 ms
        size_t n = 0;
        for (size_t i = win; i + 1 < out_.size(); ++i) {
            double energy = 0.0;
            for (size_t k = i - win; k < i; ++k) {
                energy += static_cast<double>(out_[k]) * out_[k];
            }
            const double rms = std::sqrt(energy / static_cast<double>(win));
            if (rms < 1e-4) {
                continue; // silence: a step out of nothing is the stream starting
            }
            if (std::fabs(static_cast<double>(out_[i + 1]) - out_[i]) > 4.0 * rms) {
                ++n;
                i += win; // one artefact, not one per sample of it
            }
        }
        return n;
    }

    double silence_ratio() const
    {
        if (out_.empty()) {
            return 0.0;
        }
        size_t silent = 0;
        for (float v : out_) {
            if (std::fabs(v) < 1e-5f) {
                ++silent;
            }
        }
        return static_cast<double>(silent) / static_cast<double>(out_.size());
    }

    int64_t coarse_lag() const
    {
        const size_t win = std::min<size_t>(out_.size(), static_cast<size_t>(rate_) / 5);
        if (win == 0 || ref_.size() < win) {
            return 0;
        }
        double best = -1e300;
        int64_t best_lag = 0;
        const auto limit = static_cast<int64_t>(std::min(ref_.size() - win, size_t { 96000 }));
        for (int64_t r = 0; r < limit; r += 16) {
            const double c = correlate(0, static_cast<size_t>(r), win);
            if (c > best) {
                best = c;
                best_lag = r;
            }
        }
        return best_lag;
    }

    double correlate(size_t out_off, size_t ref_off, size_t n) const
    {
        double dot = 0.0, eo = 0.0, er = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double a = out_[out_off + i];
            const double b = ref_[ref_off + i];
            dot += a * b;
            eo += a * a;
            er += b * b;
        }
        const double denom = std::sqrt(eo * er);
        return denom > 0.0 ? dot / denom : 0.0;
    }

    // Gain-normalised segmental SNR: volume is applied downstream and is not a
    // quality defect.
    double segment_snr(size_t out_off, size_t ref_off, size_t n) const
    {
        double dot = 0.0, er = 0.0, eo = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double a = out_[out_off + i];
            const double b = ref_[ref_off + i];
            dot += a * b;
            er += b * b;
            eo += a * a;
        }
        if (er < 1e-9 || eo < 1e-9) {
            return std::nan(""); // silence in either signal says nothing
        }
        const double g = dot / er;
        double noise = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double d = static_cast<double>(out_[out_off + i]) - g * ref_[ref_off + i];
            noise += d * d;
        }
        if (noise < 1e-12) {
            return 60.0;
        }
        return std::clamp(10.0 * std::log10(g * g * er / noise), -20.0, 60.0);
    }

    std::vector<float> ref_;
    std::vector<float> out_;
    int rate_;
};

// --- A/V sync ------------------------------------------------------------

struct SyncMetrics {
    int64_t median_skew_ms = 0;
    int64_t p05_skew_ms = 0;
    int64_t p95_skew_ms = 0;
    int64_t max_abs_skew_ms = 0;
    size_t samples = 0;
};

// Skew is (video shown) - (audio playing) on the sender's own timeline, so a
// negative value means the audio is ahead of the picture.
class SyncAnalyzer {
public:
    void sample(int64_t video_sender_ts_us, int64_t audio_playout_ts_us)
    {
        skew_ms_.push_back((video_sender_ts_us - audio_playout_ts_us) / 1000);
    }

    SyncMetrics finish() const
    {
        SyncMetrics m;
        m.samples = skew_ms_.size();
        if (skew_ms_.empty()) {
            return m;
        }
        m.median_skew_ms = percentile_of(skew_ms_, 50);
        m.p05_skew_ms = percentile_of(skew_ms_, 5);
        m.p95_skew_ms = percentile_of(skew_ms_, 95);
        for (int64_t v : skew_ms_) {
            m.max_abs_skew_ms = std::max(m.max_abs_skew_ms, std::abs(v));
        }
        return m;
    }

private:
    std::vector<int64_t> skew_ms_;
};

} // namespace test_util
