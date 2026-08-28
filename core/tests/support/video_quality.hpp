#pragma once

// Full-reference video quality over the in-process loopback: owning I420
// frames, libyuv PSNR/SSIM (the same functions upstream's
// rtc_tools/frame_analyzer calls), a bounded reference store keyed by marker
// frame index, and an accumulator for the metrics only markers can provide —
// per-frame PSNR/SSIM, capture-to-render delay and the dropped-index span.
// Freeze counts and framerate continuity come from libwebrtc's own
// VideoReceiveStats (media_metrics.hpp), not from re-derived math here.

#include "frame_marker.hpp"
#include "media_dump.hpp"

#include "webrtc/google_webrtc_screen_session.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace test_util {

struct I420Frame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> y;
    std::vector<uint8_t> u;
    std::vector<uint8_t> v;

    [[nodiscard]] int y_stride() const noexcept { return width; }
    [[nodiscard]] int chroma_stride() const noexcept
    {
        return (width + 1) / 2;
    }
};

// Same conversion family the production capture path uses, so an identity
// comparison saturates at the PSNR cap instead of measuring colorspace skew.
[[nodiscard]] I420Frame bgra_to_i420(std::span<const uint8_t> bgra,
    int width,
    int height,
    int stride);

// PSNR capped at kPsnrCapDb for identical frames (upstream convention).
inline constexpr double kPsnrCapDb = 48.0;
[[nodiscard]] double i420_psnr(const I420Frame& reference,
    const driscord::media::DecodedVideoFrameView& received);
[[nodiscard]] double i420_ssim(const I420Frame& reference,
    const driscord::media::DecodedVideoFrameView& received);

// Bounded reference ring keyed by frame index: the generator adds every
// submitted frame with its capture wall time, the receive side looks up by
// decoded marker index.
class ReferenceFrameStore {
public:
    explicit ReferenceFrameStore(size_t capacity = 120)
        : capacity_(capacity)
    {
    }

    void add(uint32_t frame_index,
        I420Frame frame,
        std::chrono::steady_clock::time_point capture_time)
    {
        frames_.emplace(frame_index,
            Entry { std::move(frame), capture_time });
        while (frames_.size() > capacity_) {
            frames_.erase(frames_.begin());
        }
    }

    struct Entry {
        I420Frame frame;
        std::chrono::steady_clock::time_point capture_time;
    };

    [[nodiscard]] const Entry* find(uint32_t frame_index) const
    {
        const auto found = frames_.find(frame_index);
        return found == frames_.end() ? nullptr : &found->second;
    }

private:
    size_t capacity_;
    std::map<uint32_t, Entry> frames_;
};

struct VideoQualityReport {
    size_t compared_frames = 0;
    size_t undecodable_markers = 0;
    double psnr_mean = 0.0;
    double psnr_min = 0.0;
    double ssim_mean = 0.0;
    double ssim_min = 0.0;
    // Unique decoded indexes vs the covered index span.
    size_t dropped_frames = 0;
    // Capture-to-callback on the shared in-process clock.
    std::vector<double> e2e_delay_ms;
};

// Feed every decoded frame; the marker index selects the reference.
class VideoQualityAccumulator {
public:
    explicit VideoQualityAccumulator(const ReferenceFrameStore& references,
        int reference_width,
        int reference_height)
        : references_(references)
        , reference_width_(reference_width)
        , reference_height_(reference_height)
    {
    }

    // Writes every compared pair to <label>.ref.y4m / <label>.recv.y4m in
    // comparison order, so ffmpeg's positional psnr/ssim filters see aligned
    // streams (scripts/media_metrics_crosscheck.sh).
    void enable_dump(const std::filesystem::path& directory,
        std::string_view label);

    void on_decoded(const driscord::media::DecodedVideoFrameView& frame,
        std::chrono::steady_clock::time_point receive_time);

    [[nodiscard]] VideoQualityReport report() const;

private:
    const ReferenceFrameStore& references_;
    int reference_width_;
    int reference_height_;
    std::vector<double> psnr_;
    std::vector<double> ssim_;
    std::vector<double> e2e_delay_ms_;
    std::optional<uint32_t> min_index_;
    std::optional<uint32_t> max_index_;
    size_t unique_indexes_ = 0;
    std::map<uint32_t, bool> seen_indexes_;
    size_t undecodable_markers_ = 0;
    Y4mWriter reference_dump_;
    Y4mWriter received_dump_;
};

} // namespace test_util
