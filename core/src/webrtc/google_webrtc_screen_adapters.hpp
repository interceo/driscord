#pragma once

#include "webrtc/google_webrtc_screen_session.hpp"
#include "webrtc/google_webrtc_voice_adapters.hpp"

#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#include "api/video/adapted_video_track_source.h"
#include "api/video/video_sink_interface.h"
#include "common_video/include/video_frame_buffer_pool.h"
#include "modules/desktop_capture/desktop_capturer.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace webrtc {
class Thread;
}

namespace driscord::media::detail {

class DesktopVideoSource
    : public webrtc::AdaptedVideoTrackSource,
      private webrtc::DesktopCapturer::Callback {
public:
    using ErrorCallback = std::function<void(std::string)>;

    explicit DesktopVideoSource(ErrorCallback on_error);
    ~DesktopVideoSource() override;

    bool submit_bgra(std::span<const uint8_t> bgra,
        int width,
        int height,
        int stride,
        int64_t timestamp_us);

    [[nodiscard]] std::vector<DesktopCaptureSource> sources(
        DesktopCaptureKind kind) const;
    [[nodiscard]] static DesktopCaptureThumbnail grab_thumbnail(
        DesktopCaptureKind kind,
        int64_t source_id,
        int max_width,
        int max_height);
    bool start_capture(DesktopCaptureKind kind,
        int64_t source_id,
        int max_fps,
        int max_width,
        int max_height);
    void stop_capture() noexcept;

    webrtc::MediaSourceInterface::SourceState state() const override;
    bool remote() const override;
    bool is_screencast() const override;
    std::optional<bool> needs_denoising() const override;

private:
    static std::unique_ptr<webrtc::DesktopCapturer> make_capturer(
        DesktopCaptureKind kind);
    void report_error(std::string message) const;
    void OnCaptureResult(webrtc::DesktopCapturer::Result result,
        std::unique_ptr<webrtc::DesktopFrame> frame) override;

    const ErrorCallback on_error_;
    std::mutex capture_mutex_;
    std::jthread capture_thread_;
    std::atomic<bool> capture_failed_ { false };
    std::atomic<int> max_width_ { 0 };
    std::atomic<int> max_height_ { 0 };
    webrtc::VideoFrameBufferPool buffer_pool_;
};

class DecodedVideoSink final
    : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
    using Callback = std::function<void(
        std::string_view mid, DecodedVideoFrameView frame)>;

    DecodedVideoSink(std::string mid, Callback callback);

    [[nodiscard]] const std::string& mid() const noexcept;
    void OnFrame(const webrtc::VideoFrame& frame) override;

private:
    const std::string mid_;
    const Callback callback_;
};

class LocalVideoSink final {
public:
    using Callback = std::function<void(DecodedVideoFrameView frame)>;

    explicit LocalVideoSink(Callback callback);
    ~LocalVideoSink();

    LocalVideoSink(const LocalVideoSink&) = delete;
    LocalVideoSink& operator=(const LocalVideoSink&) = delete;

    bool attach(webrtc::scoped_refptr<webrtc::VideoTrackInterface> track);
    void clear() noexcept;

private:
    const Callback callback_;
    std::mutex binding_mutex_;
    uint64_t generation_ = 0;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track_;
    std::unique_ptr<DecodedVideoSink> sink_;
};

class RemoteScreenSinks final {
public:
    using VideoCallback = std::function<void(
        std::string_view mid, DecodedVideoFrameView frame)>;
    using AudioCallback = std::function<void(
        std::string_view mid, DecodedAudioFrameView frame)>;

    RemoteScreenSinks(VideoCallback on_video, AudioCallback on_audio,
        webrtc::Thread* signaling_thread);
    ~RemoteScreenSinks();

    RemoteScreenSinks(const RemoteScreenSinks&) = delete;
    RemoteScreenSinks& operator=(const RemoteScreenSinks&) = delete;

    bool attach_video(std::string mid,
        webrtc::scoped_refptr<webrtc::VideoTrackInterface> track);
    bool attach_audio(std::string mid,
        webrtc::scoped_refptr<webrtc::AudioTrackInterface> track);
    bool set_audio_enabled(std::string_view mid, bool enabled);
    bool set_audio_volume(std::string_view mid, double volume);
    void clear() noexcept;

private:
    struct VideoBinding {
        webrtc::scoped_refptr<webrtc::VideoTrackInterface> track;
        std::unique_ptr<DecodedVideoSink> sink;
    };
    struct AudioBinding {
        std::string mid;
        webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
        std::unique_ptr<DecodedAudioSink> sink;
    };

    const VideoCallback on_video_;
    const AudioCallback on_audio_;
    webrtc::Thread* const signaling_thread_;
    std::mutex sinks_mutex_;
    std::vector<VideoBinding> video_;
    std::vector<AudioBinding> audio_;
};

[[nodiscard]] ScreenSessionStats parse_screen_stats(
    const webrtc::RTCStatsReport& report);

}
