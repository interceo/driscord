#include "video_quality.hpp"

#include "libyuv/compare.h"
#include "libyuv/convert.h"

#include <algorithm>
#include <numeric>
#include <string>

namespace test_util {

I420Frame bgra_to_i420(std::span<const uint8_t> bgra,
    int width,
    int height,
    int stride)
{
    I420Frame frame;
    frame.width = width;
    frame.height = height;
    frame.y.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    const size_t chroma
        = static_cast<size_t>((width + 1) / 2) * static_cast<size_t>((height + 1) / 2);
    frame.u.resize(chroma);
    frame.v.resize(chroma);
    // libyuv "ARGB" is B,G,R,A in memory on little-endian — the BGRA layout
    // the capture path uses.
    libyuv::ARGBToI420(bgra.data(), stride,
        frame.y.data(), frame.y_stride(),
        frame.u.data(), frame.chroma_stride(),
        frame.v.data(), frame.chroma_stride(),
        width, height);
    return frame;
}

namespace {

    bool comparable(const I420Frame& reference,
        const driscord::media::DecodedVideoFrameView& received)
    {
        return reference.width == received.width
            && reference.height == received.height && received.y != nullptr
            && received.u != nullptr && received.v != nullptr;
    }

} // namespace

double i420_psnr(const I420Frame& reference,
    const driscord::media::DecodedVideoFrameView& received)
{
    if (!comparable(reference, received)) {
        return 0.0;
    }
    const double psnr = libyuv::I420Psnr(
        reference.y.data(), reference.y_stride(),
        reference.u.data(), reference.chroma_stride(),
        reference.v.data(), reference.chroma_stride(),
        received.y, received.y_stride,
        received.u, received.u_stride,
        received.v, received.v_stride,
        reference.width, reference.height);
    return std::min(psnr, kPsnrCapDb);
}

double i420_ssim(const I420Frame& reference,
    const driscord::media::DecodedVideoFrameView& received)
{
    if (!comparable(reference, received)) {
        return 0.0;
    }
    return libyuv::I420Ssim(
        reference.y.data(), reference.y_stride(),
        reference.u.data(), reference.chroma_stride(),
        reference.v.data(), reference.chroma_stride(),
        received.y, received.y_stride,
        received.u, received.u_stride,
        received.v, received.v_stride,
        reference.width, reference.height);
}

void VideoQualityAccumulator::enable_dump(
    const std::filesystem::path& directory,
    std::string_view label)
{
    const std::string base { label };
    (void)reference_dump_.open(directory / (base + ".ref.y4m"),
        reference_width_, reference_height_);
    (void)received_dump_.open(directory / (base + ".recv.y4m"),
        reference_width_, reference_height_);
}

void VideoQualityAccumulator::on_decoded(
    const driscord::media::DecodedVideoFrameView& frame,
    std::chrono::steady_clock::time_point receive_time)
{
    const auto marker
        = decode_frame_marker(frame, reference_width_, reference_height_);
    if (!marker) {
        ++undecodable_markers_;
        return;
    }
    if (!seen_indexes_.emplace(marker->frame_index, true).second) {
        return;
    }
    ++unique_indexes_;
    min_index_ = min_index_ ? std::min(*min_index_, marker->frame_index)
                            : marker->frame_index;
    max_index_ = max_index_ ? std::max(*max_index_, marker->frame_index)
                            : marker->frame_index;

    const auto* reference = references_.find(marker->frame_index);
    if (reference == nullptr) {
        return;
    }
    psnr_.push_back(i420_psnr(reference->frame, frame));
    ssim_.push_back(i420_ssim(reference->frame, frame));
    e2e_delay_ms_.push_back(
        std::chrono::duration_cast<std::chrono::microseconds>(
            receive_time - reference->capture_time)
            .count()
        / 1'000.0);
    if (reference_dump_.is_open() && comparable(reference->frame, frame)) {
        (void)reference_dump_.write_frame(reference->frame.y.data(),
            reference->frame.y_stride(), reference->frame.u.data(),
            reference->frame.chroma_stride(), reference->frame.v.data(),
            reference->frame.chroma_stride());
        (void)received_dump_.write_frame(frame.y, frame.y_stride, frame.u,
            frame.u_stride, frame.v, frame.v_stride);
    }
}

VideoQualityReport VideoQualityAccumulator::report() const
{
    VideoQualityReport result;
    result.compared_frames = psnr_.size();
    result.undecodable_markers = undecodable_markers_;
    if (!psnr_.empty()) {
        result.psnr_mean = std::accumulate(psnr_.begin(), psnr_.end(), 0.0)
            / static_cast<double>(psnr_.size());
        result.psnr_min = *std::min_element(psnr_.begin(), psnr_.end());
        result.ssim_mean = std::accumulate(ssim_.begin(), ssim_.end(), 0.0)
            / static_cast<double>(ssim_.size());
        result.ssim_min = *std::min_element(ssim_.begin(), ssim_.end());
    }
    if (min_index_ && max_index_) {
        const size_t span
            = static_cast<size_t>(*max_index_ - *min_index_) + 1;
        result.dropped_frames
            = span > unique_indexes_ ? span - unique_indexes_ : 0;
    }
    result.e2e_delay_ms = e2e_delay_ms_;
    return result;
}

} // namespace test_util
