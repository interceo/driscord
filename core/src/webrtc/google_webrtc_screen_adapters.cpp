#include "webrtc/google_webrtc_screen_adapters.hpp"

#include "webrtc/google_webrtc_stats_lift.hpp"

#include "api/stats/rtcstats_objects.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "libyuv/convert.h"
#include "libyuv/convert_argb.h"
#include "libyuv/convert_from_argb.h"
#include "libyuv/scale_argb.h"
#include "modules/desktop_capture/desktop_capture_options.h"
#include "modules/desktop_capture/desktop_frame.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <future>
#include <limits>
#include <utility>

namespace driscord::media::detail {

DesktopVideoSource::DesktopVideoSource(ErrorCallback on_error)
    : webrtc::AdaptedVideoTrackSource(2)
    , on_error_(std::move(on_error))
{
}

DesktopVideoSource::~DesktopVideoSource()
{
    stop_capture();
    capture_failed_ = false;
}

webrtc::MediaSourceInterface::SourceState DesktopVideoSource::state() const
{
    return webrtc::MediaSourceInterface::kLive;
}

bool DesktopVideoSource::remote() const
{
    return false;
}

bool DesktopVideoSource::is_screencast() const
{
    return true;
}

std::optional<bool> DesktopVideoSource::needs_denoising() const
{
    return false;
}

void DesktopVideoSource::report_error(std::string message) const
{
    if (on_error_) {
        on_error_(std::move(message));
    }
}

bool DesktopVideoSource::submit_bgra(std::span<const uint8_t> bgra,
    int width,
    int height,
    int stride,
    int64_t timestamp_us)
{
    if (width <= 0 || height <= 0
        || width > std::numeric_limits<int>::max() / 4
        || stride < width * 4
        || timestamp_us < 0
        || static_cast<size_t>(stride)
            > std::numeric_limits<size_t>::max()
                / static_cast<size_t>(height)
        || bgra.size()
            < static_cast<size_t>(stride) * static_cast<size_t>(height)) {
        return false;
    }

    const int max_width = max_width_;
    const int max_height = max_height_;
    std::vector<uint8_t> constrained;
    if (max_width > 0 && max_height > 0
        && (width > max_width || height > max_height)) {
        const double scale = std::min(
            static_cast<double>(max_width) / static_cast<double>(width),
            static_cast<double>(max_height) / static_cast<double>(height));
        const int constrained_width = std::max(1,
            static_cast<int>(std::floor(static_cast<double>(width) * scale)));
        const int constrained_height = std::max(1,
            static_cast<int>(std::floor(static_cast<double>(height) * scale)));
        constrained.resize(static_cast<size_t>(constrained_width)
            * static_cast<size_t>(constrained_height) * 4);
        if (libyuv::ARGBScale(bgra.data(), stride, width, height,
                constrained.data(), constrained_width * 4,
                constrained_width, constrained_height,
                libyuv::kFilterBilinear)
            != 0) {
            return false;
        }
        bgra = constrained;
        width = constrained_width;
        height = constrained_height;
        stride = width * 4;
    }

    int out_width = 0;
    int out_height = 0;
    int crop_width = 0;
    int crop_height = 0;
    int crop_x = 0;
    int crop_y = 0;
    if (!AdaptFrame(width, height, timestamp_us, &out_width, &out_height,
            &crop_width, &crop_height, &crop_x, &crop_y)) {
        return false;
    }

    auto input = buffer_pool_.CreateI420Buffer(width, height);
    if (!input) {
        return false;
    }
    // libyuv's ARGB name denotes the register word; on little-endian desktop
    // platforms its byte layout is exactly the BGRA emitted by DesktopFrame.
    if (libyuv::ARGBToI420(bgra.data(), stride, input->MutableDataY(),
            input->StrideY(), input->MutableDataU(), input->StrideU(),
            input->MutableDataV(), input->StrideV(), width, height)
        != 0) {
        return false;
    }

    webrtc::scoped_refptr<webrtc::I420BufferInterface> output = input;
    if (crop_x != 0 || crop_y != 0 || crop_width != width
        || crop_height != height || out_width != width
        || out_height != height) {
        auto adapted = buffer_pool_.CreateI420Buffer(out_width, out_height);
        if (!adapted) {
            return false;
        }
        adapted->CropAndScaleFrom(*input, crop_x, crop_y, crop_width,
            crop_height);
        output = std::move(adapted);
    }

    OnFrame(webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(output)
            .set_timestamp_us(timestamp_us)
            .set_rotation(webrtc::kVideoRotation_0)
            .set_content_type(webrtc::VideoContentType::SCREENSHARE)
            .build());
    return true;
}

std::unique_ptr<webrtc::DesktopCapturer> DesktopVideoSource::make_capturer(
    DesktopCaptureKind kind)
{
    const auto options = webrtc::DesktopCaptureOptions::CreateDefault();
    return kind == DesktopCaptureKind::Screen
        ? webrtc::DesktopCapturer::CreateScreenCapturer(options)
        : webrtc::DesktopCapturer::CreateWindowCapturer(options);
}

std::vector<DesktopCaptureSource> DesktopVideoSource::sources(
    DesktopCaptureKind kind) const
{
    std::vector<DesktopCaptureSource> result;
    auto capturer = make_capturer(kind);
    if (!capturer) {
        return result;
    }
    webrtc::DesktopCapturer::SourceList native_sources;
    if (!capturer->GetSourceList(&native_sources)) {
        return result;
    }
    result.reserve(native_sources.size());
    for (auto& source : native_sources) {
        result.push_back({ static_cast<int64_t>(source.id),
            std::move(source.title) });
    }
    return result;
}

DesktopCaptureThumbnail DesktopVideoSource::grab_thumbnail(
    DesktopCaptureKind kind,
    int64_t source_id,
    int max_width,
    int max_height)
{
    if (max_width <= 0 || max_height <= 0) {
        return { };
    }
    const auto native_id = static_cast<webrtc::DesktopCapturer::SourceId>(source_id);
    if (static_cast<int64_t>(native_id) != source_id) {
        return { };
    }

    class OneShotCallback final : public webrtc::DesktopCapturer::Callback {
    public:
        void OnCaptureResult(webrtc::DesktopCapturer::Result next_result,
            std::unique_ptr<webrtc::DesktopFrame> next_frame) override
        {
            {
                std::scoped_lock lock(mutex);
                result = next_result;
                frame = std::move(next_frame);
                done = true;
            }
            ready.notify_one();
        }

        std::mutex mutex;
        std::condition_variable ready;
        webrtc::DesktopCapturer::Result result = webrtc::DesktopCapturer::Result::ERROR_TEMPORARY;
        std::unique_ptr<webrtc::DesktopFrame> frame;
        bool done = false;
    } callback;

    auto capturer = make_capturer(kind);
    if (!capturer || !capturer->SelectSource(native_id)) {
        return { };
    }
    capturer->Start(&callback);
    capturer->CaptureFrame();
    std::unique_lock lock(callback.mutex);
    if (!callback.ready.wait_for(lock, std::chrono::seconds(2),
            [&callback] { return callback.done; })
        || callback.result != webrtc::DesktopCapturer::Result::SUCCESS
        || !callback.frame || !callback.frame->data()) {
        return { };
    }

    const int source_width = callback.frame->size().width();
    const int source_height = callback.frame->size().height();
    if (source_width <= 0 || source_height <= 0
        || callback.frame->stride() < source_width * 4) {
        return { };
    }
    const double scale = std::min({ 1.0,
        static_cast<double>(max_width) / source_width,
        static_cast<double>(max_height) / source_height });
    DesktopCaptureThumbnail thumbnail;
    thumbnail.width = std::max(1,
        static_cast<int>(std::floor(source_width * scale)));
    thumbnail.height = std::max(1,
        static_cast<int>(std::floor(source_height * scale)));
    std::vector<uint8_t> bgra(static_cast<size_t>(thumbnail.width)
        * static_cast<size_t>(thumbnail.height) * 4);
    if (libyuv::ARGBScale(callback.frame->data(), callback.frame->stride(),
            source_width, source_height, bgra.data(), thumbnail.width * 4,
            thumbnail.width, thumbnail.height, libyuv::kFilterBilinear)
        != 0) {
        return { };
    }
    thumbnail.rgba.resize(bgra.size());
    if (libyuv::ARGBToABGR(bgra.data(), thumbnail.width * 4,
            thumbnail.rgba.data(), thumbnail.width * 4,
            thumbnail.width, thumbnail.height)
        != 0) {
        return { };
    }
    return thumbnail;
}

bool DesktopVideoSource::start_capture(DesktopCaptureKind kind,
    int64_t source_id,
    int max_fps,
    int max_width,
    int max_height)
{
    if (max_fps <= 0 || max_fps > 60 || max_width < 0 || max_height < 0
        || ((max_width == 0) != (max_height == 0))) {
        return false;
    }
    const auto native_id = static_cast<webrtc::DesktopCapturer::SourceId>(source_id);
    if (static_cast<int64_t>(native_id) != source_id) {
        return false;
    }

    stop_capture();
    max_width_ = max_width;
    max_height_ = max_height;
    capture_failed_ = false;
    auto ready = std::make_shared<std::promise<bool>>();
    auto ready_result = ready->get_future();
    {
        std::scoped_lock lock(capture_mutex_);
        capture_thread_ = std::jthread(
            [this, kind, native_id, max_fps, ready](std::stop_token stop) {
                bool ready_signaled = false;
                auto signal_ready = [&ready, &ready_signaled](bool value) {
                    if (!ready_signaled) {
                        ready->set_value(value);
                        ready_signaled = true;
                    }
                };
                try {
                    auto capturer = make_capturer(kind);
                    if (!capturer) {
                        signal_ready(false);
                        report_error("Google WebRTC desktop capturer is unavailable");
                        return;
                    }
                    if (!capturer->SelectSource(native_id)) {
                        signal_ready(false);
                        report_error("Google WebRTC desktop source selection failed");
                        return;
                    }
                    capturer->SetMaxFrameRate(static_cast<uint32_t>(max_fps));
                    capturer->Start(this);
                    signal_ready(true);
                    const auto interval = std::chrono::microseconds(1'000'000 / max_fps);
                    auto deadline = std::chrono::steady_clock::now();
                    while (!stop.stop_requested() && !capture_failed_) {
                        capturer->CaptureFrame();
                        // A capture slower than the frame interval must not put
                        // the loop into permanent catch-up: without the clamp
                        // the deadline stays in the past and every subsequent
                        // frame is taken back-to-back at 100% of a core.
                        deadline = std::max(deadline + interval,
                            std::chrono::steady_clock::now());
                        std::this_thread::sleep_until(deadline);
                    }
                } catch (const std::exception& exception) {
                    signal_ready(false);
                    report_error(std::string("Google WebRTC desktop capture failed: ")
                        + exception.what());
                } catch (...) {
                    signal_ready(false);
                    report_error("Google WebRTC desktop capture failed");
                }
            });
    }
    if (!ready_result.get()) {
        stop_capture();
        return false;
    }
    return true;
}

void DesktopVideoSource::stop_capture() noexcept
{
    std::jthread old;
    {
        std::scoped_lock lock(capture_mutex_);
        old = std::move(capture_thread_);
    }
    if (old.joinable()) {
        old.request_stop();
        old.join();
    }
    max_width_ = 0;
    max_height_ = 0;
}

void DesktopVideoSource::OnCaptureResult(
    webrtc::DesktopCapturer::Result result,
    std::unique_ptr<webrtc::DesktopFrame> frame)
{
    if (result == webrtc::DesktopCapturer::Result::ERROR_PERMANENT) {
        capture_failed_ = true;
        report_error("Google WebRTC desktop capture failed permanently");
        return;
    }
    if (result != webrtc::DesktopCapturer::Result::SUCCESS || !frame
        || !frame->data() || frame->size().width() <= 0
        || frame->size().height() <= 0 || frame->stride() <= 0) {
        return;
    }
    (void)submit_bgra(
        std::span<const uint8_t>(frame->data(),
            static_cast<size_t>(frame->stride())
                * static_cast<size_t>(frame->size().height())),
        frame->size().width(), frame->size().height(), frame->stride(),
        webrtc::TimeMicros());
}

DecodedVideoSink::DecodedVideoSink(std::string mid, Callback callback)
    : mid_(std::move(mid))
    , callback_(std::move(callback))
{
}

const std::string& DecodedVideoSink::mid() const noexcept
{
    return mid_;
}

void DecodedVideoSink::OnFrame(const webrtc::VideoFrame& frame)
{
    if (!callback_) {
        return;
    }
    // ToI420 is a no-op for the decoder's native output; the buffer keeps the
    // planes alive for the duration of the callback. No conversion, no lock:
    // the consumer copies (or uploads) what it needs.
    auto i420 = frame.video_frame_buffer()->ToI420();
    if (!i420 || i420->width() <= 0 || i420->height() <= 0) {
        return;
    }
    callback_(mid_, DecodedVideoFrameView {
                        .y = i420->DataY(),
                        .u = i420->DataU(),
                        .v = i420->DataV(),
                        .y_stride = i420->StrideY(),
                        .u_stride = i420->StrideU(),
                        .v_stride = i420->StrideV(),
                        .width = i420->width(),
                        .height = i420->height(),
                        .timestamp_us = frame.timestamp_us(),
                    });
}

LocalVideoSink::LocalVideoSink(Callback callback)
    : callback_(std::move(callback))
{
}

LocalVideoSink::~LocalVideoSink()
{
    clear();
}

bool LocalVideoSink::attach(
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track)
{
    if (!track || !callback_) {
        return false;
    }

    webrtc::scoped_refptr<webrtc::VideoTrackInterface> old_track;
    std::unique_ptr<DecodedVideoSink> old_sink;
    uint64_t generation = 0;
    {
        std::scoped_lock lock(binding_mutex_);
        if (track_ == track && sink_) {
            return true;
        }
        generation = ++generation_;
        old_track = std::move(track_);
        old_sink = std::move(sink_);
    }
    if (old_track && old_sink) {
        old_track->RemoveSink(old_sink.get());
    }

    auto candidate = std::make_unique<DecodedVideoSink>("local-preview",
        [this](std::string_view, DecodedVideoFrameView frame) {
            callback_(frame);
        });
    webrtc::VideoSinkWants wants;
    wants.rotation_applied = true;
    track->AddOrUpdateSink(candidate.get(), wants);

    bool retained = false;
    {
        std::scoped_lock lock(binding_mutex_);
        if (generation_ == generation && !track_ && !sink_) {
            track_ = track;
            sink_ = std::move(candidate);
            retained = true;
        }
    }
    if (!retained) {
        track->RemoveSink(candidate.get());
    }
    return retained;
}

void LocalVideoSink::clear() noexcept
{
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track;
    std::unique_ptr<DecodedVideoSink> sink;
    {
        std::scoped_lock lock(binding_mutex_);
        ++generation_;
        track = std::move(track_);
        sink = std::move(sink_);
    }
    if (track && sink) {
        track->RemoveSink(sink.get());
    }
}

RemoteScreenSinks::RemoteScreenSinks(
    VideoCallback on_video, AudioCallback on_audio,
    webrtc::Thread* signaling_thread)
    : on_video_(std::move(on_video))
    , on_audio_(std::move(on_audio))
    , signaling_thread_(signaling_thread)
{
}

RemoteScreenSinks::~RemoteScreenSinks()
{
    clear();
}

bool RemoteScreenSinks::attach_video(std::string mid,
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track)
{
    if (!track || !on_video_) {
        return false;
    }
    VideoBinding binding {
        .track = std::move(track),
        .sink = std::make_unique<DecodedVideoSink>(mid,
            [this](std::string_view sink_mid, DecodedVideoFrameView frame) {
                on_video_(sink_mid, frame);
            }),
    };
    webrtc::VideoSinkWants wants;
    wants.rotation_applied = true;
    binding.track->AddOrUpdateSink(binding.sink.get(), wants);
    {
        std::scoped_lock lock(sinks_mutex_);
        const auto duplicate = std::find_if(video_.begin(), video_.end(),
            [&mid](const auto& candidate) {
                return candidate.sink->mid() == mid;
            });
        if (duplicate == video_.end()) {
            video_.push_back(std::move(binding));
            return true;
        }
    }
    binding.track->RemoveSink(binding.sink.get());
    return false;
}

bool RemoteScreenSinks::attach_audio(std::string mid,
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> track)
{
    if (!track) {
        return false;
    }
    AudioBinding binding {
        .mid = mid,
        .track = std::move(track),
        .sink = on_audio_ ? std::make_unique<DecodedAudioSink>(mid,
                                [this](std::string_view sink_mid,
                                    const void* audio_data,
                                    int bits_per_sample,
                                    int sample_rate,
                                    size_t channels,
                                    size_t frames) {
                                    if (!audio_data || bits_per_sample != 16 || sample_rate <= 0
                                        || channels == 0 || frames == 0
                                        || frames
                                            > std::numeric_limits<size_t>::max() / channels) {
                                        return;
                                    }
                                    on_audio_(sink_mid,
                                        DecodedAudioFrameView {
                                            .samples = std::span<const int16_t>(
                                                static_cast<const int16_t*>(audio_data),
                                                frames * channels),
                                            .sample_rate_hz = sample_rate,
                                            .channels = channels,
                                            .frames = frames,
                                        });
                                })
                          : nullptr,
    };
    if (binding.sink) {
        binding.track->AddSink(binding.sink.get());
    }
    {
        std::scoped_lock lock(sinks_mutex_);
        const auto duplicate = std::find_if(audio_.begin(), audio_.end(),
            [&mid](const auto& candidate) {
                return candidate.mid == mid;
            });
        if (duplicate == audio_.end()) {
            audio_.push_back(std::move(binding));
            return true;
        }
    }
    if (binding.sink) {
        binding.track->RemoveSink(binding.sink.get());
    }
    return false;
}

bool RemoteScreenSinks::set_audio_enabled(
    std::string_view mid, bool enabled)
{
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
    {
        std::scoped_lock lock(sinks_mutex_);
        const auto it = std::find_if(audio_.begin(), audio_.end(),
            [mid](const AudioBinding& binding) { return binding.mid == mid; });
        if (it != audio_.end()) {
            track = it->track;
        }
    }
    return track && track->set_enabled(enabled);
}

bool RemoteScreenSinks::set_audio_volume(
    std::string_view mid, double volume)
{
    if (!std::isfinite(volume) || volume < 0.0 || volume > 10.0) {
        return false;
    }
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
    {
        std::scoped_lock lock(sinks_mutex_);
        const auto it = std::find_if(audio_.begin(), audio_.end(),
            [mid](const AudioBinding& binding) { return binding.mid == mid; });
        if (it != audio_.end()) {
            track = it->track;
        }
    }
    if (!track || !signaling_thread_) {
        return false;
    }
    return signaling_thread_->BlockingCall([track = std::move(track), volume] {
        auto source = track->GetSource();
        if (!source) {
            return false;
        }
        source->SetVolume(volume);
        return true;
    });
}

void RemoteScreenSinks::clear() noexcept
{
    std::vector<VideoBinding> video;
    std::vector<AudioBinding> audio;
    {
        std::scoped_lock lock(sinks_mutex_);
        video = std::move(video_);
        audio = std::move(audio_);
    }
    for (const auto& binding : video) {
        if (binding.track && binding.sink) {
            binding.track->RemoveSink(binding.sink.get());
        }
    }
    for (const auto& binding : audio) {
        if (binding.track && binding.sink) {
            binding.track->RemoveSink(binding.sink.get());
        }
    }
}

ScreenSessionStats parse_screen_stats(const webrtc::RTCStatsReport& report)
{
    ScreenSessionStats result;
    for (const auto* outbound :
        report.GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
        const bool video = outbound->kind && *outbound->kind == "video";
        if (video) {
            result.video_packets_sent += outbound->packets_sent.value_or(0);
            result.video_bytes_sent += outbound->bytes_sent.value_or(0);
            result.video_frames_encoded += outbound->frames_encoded.value_or(0);
            result.video_key_frames_encoded
                += outbound->key_frames_encoded.value_or(0);
            result.video_target_bitrate_bps
                = outbound->target_bitrate.value_or(-1.0);
            result.video_quality_limitation = lift_quality_limitation(*outbound);
            if (outbound->quality_limitation_durations.has_value()) {
                const auto& durations = *outbound->quality_limitation_durations;
                if (const auto it = durations.find("bandwidth");
                    it != durations.end()) {
                    result.video_quality_limitation_bandwidth_seconds
                        = it->second;
                }
            }
        } else if (!outbound->kind || *outbound->kind == "audio") {
            result.audio_packets_sent += outbound->packets_sent.value_or(0);
            result.audio_bytes_sent += outbound->bytes_sent.value_or(0);
        }
    }
    for (const auto* inbound :
        report.GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
        const bool video = inbound->kind && *inbound->kind == "video";
        if (inbound->kind && !video && *inbound->kind != "audio") {
            continue;
        }
        ScreenInboundRtpStats stats {
            .mid = inbound->mid.value_or(std::string { }),
            .video = video,
            .packets_received = inbound->packets_received.value_or(0),
            .bytes_received = inbound->bytes_received.value_or(0),
            .packets_lost = inbound->packets_lost.value_or(0),
            .jitter_buffer_emitted_count = inbound->jitter_buffer_emitted_count.value_or(0),
            .jitter_buffer_delay_seconds = inbound->jitter_buffer_delay.value_or(0.0),
            .jitter_buffer_target_delay_seconds = inbound->jitter_buffer_target_delay.value_or(0.0),
            .concealed_samples = inbound->concealed_samples.value_or(0),
            .frames_decoded = inbound->frames_decoded.value_or(0),
            .frames_dropped = inbound->frames_dropped.value_or(0),
            .key_frames_decoded = inbound->key_frames_decoded.value_or(0),
            .rtp = lift_rtp_receive_stats(*inbound),
        };
        if (video) {
            stats.video_playback = lift_video_receive_stats(*inbound);
        } else {
            stats.audio = lift_audio_receive_stats(*inbound);
        }
        result.inbound.push_back(std::move(stats));
    }
    for (const auto* pair :
        report.GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
        if (!pair->nominated.value_or(false)) {
            continue;
        }
        result.available_outgoing_bitrate_bps
            = pair->available_outgoing_bitrate.value_or(-1.0);
        break;
    }
    std::sort(result.inbound.begin(), result.inbound.end(),
        [](const ScreenInboundRtpStats& left,
            const ScreenInboundRtpStats& right) {
            return left.mid < right.mid;
        });
    return result;
}

} // namespace driscord::media::detail
