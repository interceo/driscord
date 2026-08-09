#include "webrtc/google_webrtc_screen_session.hpp"

#include "webrtc/google_webrtc_peer_adapters.hpp"
#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_runtime_internal.hpp"
#include "webrtc/google_webrtc_screen_adapters.hpp"
#include "webrtc/google_webrtc_voice_adapters.hpp"

#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/video/video_source_interface.h"
#include "api/video_track_source_proxy_factory.h"

#include <mutex>
#include <utility>
#include <vector>

namespace driscord::media {
struct GoogleWebRtcScreenSession::Impl final
    : webrtc::PeerConnectionObserver,
      std::enable_shared_from_this<Impl> {
    std::shared_ptr<GoogleWebRtcRuntime::Impl> runtime;
    const ScreenSessionConfig config;
    const ScreenSessionCallbacks callbacks;

    mutable std::mutex mutex;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
    webrtc::scoped_refptr<detail::DesktopVideoSource> video_source;
    webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>
        proxied_video_source;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track;
    webrtc::scoped_refptr<webrtc::AudioSourceInterface> audio_source;
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track;
    std::shared_ptr<detail::RemoteScreenSinks> remote_sinks;
    bool started = false;
    bool start_attempted = false;
    bool close_requested = false;
    bool desired_sharing_enabled;
    bool desired_system_audio_enabled;

    Impl(std::shared_ptr<GoogleWebRtcRuntime::Impl> runtime_value,
        ScreenSessionConfig config_value,
        ScreenSessionCallbacks callbacks_value)
        : runtime(std::move(runtime_value))
        , config(config_value)
        , callbacks(std::move(callbacks_value))
        , desired_sharing_enabled(config.sharing_enabled)
        , desired_system_audio_enabled(config.system_audio_enabled)
    {
    }

    ~Impl() override { close(); }

    void report_error(std::string message) const
    {
        if (callbacks.on_error) {
            callbacks.on_error(std::move(message));
        }
    }

    bool start()
    {
        if (config.remote_stream_slots > kMaxRemoteStreamSlots) {
            report_error("screen receive slot count exceeds the safety limit");
            return false;
        }
        if (config.max_video_bitrate_bps <= 0
            || config.max_video_bitrate_bps > 50'000'000) {
            report_error("screen video bitrate is outside the safety range");
            return false;
        }
        if (config.max_system_audio_bitrate_bps <= 0
            || config.max_system_audio_bitrate_bps > 510'000) {
            report_error("screen audio bitrate is outside the Opus range");
            return false;
        }
        if (desired_system_audio_enabled
            && (!runtime || !runtime->injected_pcm_source)) {
            report_error("screen system audio requires an injected-audio runtime");
            return false;
        }

        bool runtime_ready = false;
        {
            std::scoped_lock lock(mutex);
            if (start_attempted || close_requested) {
                return started;
            }
            start_attempted = true;
            runtime_ready = runtime && runtime->factory;
        }
        if (!runtime_ready) {
            report_error("Google WebRTC runtime is not ready");
            return false;
        }

        webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
        rtc_config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
        rtc_config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
        rtc_config.rtcp_mux_policy = webrtc::PeerConnectionInterface::kRtcpMuxPolicyRequire;

        webrtc::PeerConnectionDependencies dependencies(this);
        auto pc_result = runtime->factory->CreatePeerConnectionOrError(
            rtc_config, std::move(dependencies));
        if (!pc_result.ok()) {
            report_error(detail::rtc_error_message(
                "screen PeerConnection creation failed", pc_result.error()));
            return false;
        }
        auto new_pc = pc_result.MoveValue();

        const std::weak_ptr<Impl> weak = weak_from_this();
        auto new_video_source = webrtc::make_ref_counted<detail::DesktopVideoSource>(
            [weak](std::string message) {
                if (auto self = weak.lock()) {
                    self->report_error(std::move(message));
                }
            });
        auto new_remote_sinks = std::make_shared<detail::RemoteScreenSinks>(
            [weak](std::string_view mid, DecodedVideoFrameView frame) {
                if (auto self = weak.lock()) {
                    {
                        std::scoped_lock lock(self->mutex);
                        if (!self->started) {
                            return;
                        }
                    }
                    if (self->callbacks.on_remote_video) {
                        self->callbacks.on_remote_video(mid, frame);
                    }
                }
            },
            [weak](std::string_view mid, DecodedAudioFrameView frame) {
                if (auto self = weak.lock()) {
                    {
                        std::scoped_lock lock(self->mutex);
                        if (!self->started) {
                            return;
                        }
                    }
                    if (self->callbacks.on_remote_audio) {
                        self->callbacks.on_remote_audio(mid, frame);
                    }
                }
            });
        auto new_proxied_source = webrtc::CreateVideoTrackSourceProxy(
            runtime->signaling_thread.get(), runtime->worker_thread.get(),
            new_video_source.get());
        auto new_video_track = runtime->factory->CreateVideoTrack(
            new_proxied_source, "driscord-screen-video");
        if (!new_video_track) {
            report_error("screen video track creation failed");
            new_pc->Close();
            return false;
        }
        new_video_track->set_content_hint(
            webrtc::VideoTrackInterface::ContentHint::kDetailed);
        new_video_track->set_enabled(desired_sharing_enabled);

        webrtc::AudioOptions audio_options;
        audio_options.echo_cancellation = false;
        audio_options.auto_gain_control = false;
        audio_options.noise_suppression = false;
        audio_options.highpass_filter = false;
        auto new_audio_source = runtime->factory->CreateAudioSource(audio_options);
        auto new_audio_track = runtime->factory->CreateAudioTrack(
            "driscord-system-audio", new_audio_source.get());
        if (!new_audio_source || !new_audio_track) {
            report_error("screen system-audio track creation failed");
            new_pc->Close();
            return false;
        }
        new_audio_track->set_enabled(desired_system_audio_enabled);

        webrtc::RtpTransceiverInit video_send_init;
        video_send_init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
        video_send_init.stream_ids = { "driscord-screen" };
        webrtc::RtpEncodingParameters video_encoding;
        video_encoding.max_bitrate_bps = config.max_video_bitrate_bps;
        video_send_init.send_encodings.push_back(std::move(video_encoding));
        auto video_sender = new_pc->AddTransceiver(new_video_track, video_send_init);
        if (!video_sender.ok()) {
            report_error(detail::rtc_error_message(
                "screen video transceiver creation failed",
                video_sender.error()));
            new_pc->Close();
            return false;
        }

        webrtc::RtpTransceiverInit audio_send_init;
        audio_send_init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
        audio_send_init.stream_ids = { "driscord-screen" };
        webrtc::RtpEncodingParameters audio_encoding;
        audio_encoding.max_bitrate_bps = config.max_system_audio_bitrate_bps;
        audio_send_init.send_encodings.push_back(std::move(audio_encoding));
        auto audio_sender = new_pc->AddTransceiver(new_audio_track, audio_send_init);
        if (!audio_sender.ok()) {
            report_error(detail::rtc_error_message(
                "screen audio transceiver creation failed",
                audio_sender.error()));
            new_pc->Close();
            return false;
        }

        webrtc::RtpTransceiverInit receive_init;
        receive_init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
        for (size_t i = 0; i < config.remote_stream_slots; ++i) {
            auto video_receiver = new_pc->AddTransceiver(
                webrtc::MediaType::VIDEO, receive_init);
            auto audio_receiver = new_pc->AddTransceiver(
                webrtc::MediaType::AUDIO, receive_init);
            if (!video_receiver.ok() || !audio_receiver.ok()) {
                report_error("screen receive-pair transceiver creation failed");
                new_pc->Close();
                return false;
            }
        }

        bool cancelled = false;
        {
            std::scoped_lock lock(mutex);
            if (close_requested) {
                cancelled = true;
            } else {
                pc = new_pc;
                video_source = std::move(new_video_source);
                proxied_video_source = std::move(new_proxied_source);
                video_track = std::move(new_video_track);
                audio_source = std::move(new_audio_source);
                audio_track = std::move(new_audio_track);
                remote_sinks = std::move(new_remote_sinks);
                started = true;
            }
        }
        if (cancelled) {
            new_video_source->stop_capture();
            new_pc->Close();
            return false;
        }

        auto observer = webrtc::make_ref_counted<detail::SetLocalDescriptionObserver>(
            [weak = weak_from_this(), new_pc](webrtc::RTCError error) {
                if (auto self = weak.lock()) {
                    self->on_local_description(
                        new_pc, std::move(error));
                }
            });
        new_pc->SetLocalDescription(std::move(observer));
        return true;
    }

    void close() noexcept
    {
        webrtc::scoped_refptr<webrtc::PeerConnectionInterface> old_pc;
        webrtc::scoped_refptr<detail::DesktopVideoSource> old_source;
        std::shared_ptr<detail::RemoteScreenSinks> old_sinks;
        {
            std::scoped_lock lock(mutex);
            old_pc = std::move(pc);
            old_source = std::move(video_source);
            proxied_video_source = nullptr;
            video_track = nullptr;
            audio_track = nullptr;
            audio_source = nullptr;
            old_sinks = std::move(remote_sinks);
            started = false;
            close_requested = true;
        }
        if (old_source) {
            old_source->stop_capture();
        }
        if (old_sinks) {
            old_sinks->clear();
        }
        if (old_pc) {
            old_pc->Close();
        }
    }

    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> current_pc() const
    {
        std::scoped_lock lock(mutex);
        return pc;
    }

    bool owns(
        const webrtc::scoped_refptr<webrtc::PeerConnectionInterface>& other)
        const
    {
        std::scoped_lock lock(mutex);
        return started && pc == other;
    }

    void on_local_description(
        const webrtc::scoped_refptr<webrtc::PeerConnectionInterface>& expected,
        webrtc::RTCError error)
    {
        if (!owns(expected)) {
            return;
        }
        if (!error.ok()) {
            report_error(
                detail::rtc_error_message(
                    "setting screen local description failed", error));
            return;
        }
        const auto* description = expected->local_description();
        std::string sdp;
        if (!description || !description->ToString(&sdp)) {
            report_error("serializing screen local description failed");
            return;
        }
        if (callbacks.on_offer) {
            callbacks.on_offer(std::move(sdp));
        }
    }

    void on_remote_description(
        const webrtc::scoped_refptr<webrtc::PeerConnectionInterface>& expected,
        webrtc::RTCError error)
    {
        if (owns(expected) && !error.ok()) {
            report_error(
                detail::rtc_error_message(
                    "setting screen remote answer failed", error));
        }
    }

    void apply_answer(std::string_view sdp)
    {
        auto current = current_pc();
        if (!current) {
            report_error("screen answer received before session start");
            return;
        }
        webrtc::SdpParseError parse_error;
        auto description = webrtc::CreateSessionDescription(
            webrtc::SdpType::kAnswer, sdp, &parse_error);
        if (!description) {
            report_error("invalid screen answer SDP: "
                + parse_error.description);
            return;
        }
        auto observer = webrtc::make_ref_counted<detail::SetRemoteDescriptionObserver>(
            [weak = weak_from_this(), current](webrtc::RTCError error) {
                if (auto self = weak.lock()) {
                    self->on_remote_description(
                        current, std::move(error));
                }
            });
        current->SetRemoteDescription(
            std::move(description), std::move(observer));
    }

    void add_remote_candidate(std::string_view candidate,
        std::string_view mid)
    {
        auto current = current_pc();
        if (!current) {
            report_error("screen ICE candidate received before session start");
            return;
        }
        webrtc::SdpParseError parse_error;
        auto parsed = std::unique_ptr<webrtc::IceCandidate>(
            webrtc::CreateIceCandidate(
                mid, -1, std::string(candidate), &parse_error));
        if (!parsed) {
            report_error("invalid screen ICE candidate: "
                + parse_error.description);
            return;
        }
        const std::weak_ptr<Impl> weak = weak_from_this();
        current->AddIceCandidate(std::move(parsed),
            [weak, current](webrtc::RTCError error) {
                auto self = weak.lock();
                if (self && self->owns(current) && !error.ok()) {
                    self->report_error(detail::rtc_error_message(
                        "adding screen ICE candidate failed", error));
                }
            });
    }

    bool get_stats(std::function<void(ScreenSessionStats)> callback)
    {
        auto current = current_pc();
        if (!current || !callback) {
            return false;
        }
        const std::weak_ptr<Impl> weak = weak_from_this();
        auto observer = webrtc::make_ref_counted<detail::StatsObserver>(
            [weak, current, callback = std::move(callback)](
                const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
                if (auto self = weak.lock(); self && self->owns(current)
                    && report) {
                    callback(detail::parse_screen_stats(*report));
                }
            });
        current->GetStats(observer.get());
        return true;
    }

    void set_sharing_enabled(bool enabled)
    {
        webrtc::scoped_refptr<webrtc::VideoTrackInterface> track;
        {
            std::scoped_lock lock(mutex);
            desired_sharing_enabled = enabled;
            track = video_track;
        }
        if (track) {
            track->set_enabled(enabled);
        }
    }

    void set_system_audio_enabled(bool enabled)
    {
        if (enabled && (!runtime || !runtime->injected_pcm_source)) {
            report_error("screen system audio requires an injected-audio runtime");
            return;
        }
        webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
        {
            std::scoped_lock lock(mutex);
            desired_system_audio_enabled = enabled;
            track = audio_track;
        }
        if (track) {
            track->set_enabled(enabled);
        }
    }

    bool sharing_enabled() const noexcept
    {
        webrtc::scoped_refptr<webrtc::VideoTrackInterface> track;
        bool desired = false;
        {
            std::scoped_lock lock(mutex);
            track = video_track;
            desired = desired_sharing_enabled;
        }
        return track ? track->enabled() : desired;
    }

    bool submit_bgra_frame(std::span<const uint8_t> bgra,
        int width,
        int height,
        int stride,
        int64_t timestamp_us)
    {
        webrtc::scoped_refptr<detail::DesktopVideoSource> source;
        bool enabled = false;
        {
            std::scoped_lock lock(mutex);
            source = video_source;
            enabled = started && desired_sharing_enabled;
        }
        return enabled && source
            && source->submit_bgra(
                bgra, width, height, stride, timestamp_us);
    }

    std::vector<DesktopCaptureSource> desktop_sources(
        DesktopCaptureKind kind) const
    {
        webrtc::scoped_refptr<detail::DesktopVideoSource> source;
        {
            std::scoped_lock lock(mutex);
            source = video_source;
        }
        if (!source) {
            source = webrtc::make_ref_counted<detail::DesktopVideoSource>(
                detail::DesktopVideoSource::ErrorCallback { });
        }
        return source->sources(kind);
    }

    bool start_desktop_capture(DesktopCaptureKind kind,
        int64_t source_id,
        int max_fps,
        int max_width,
        int max_height)
    {
        webrtc::scoped_refptr<detail::DesktopVideoSource> source;
        {
            std::scoped_lock lock(mutex);
            source = video_source;
        }
        if (!source || !source->start_capture(kind, source_id, max_fps, max_width, max_height)) {
            return false;
        }
        set_sharing_enabled(true);
        return true;
    }

    void stop_desktop_capture() noexcept
    {
        webrtc::scoped_refptr<detail::DesktopVideoSource> source;
        {
            std::scoped_lock lock(mutex);
            source = video_source;
        }
        if (source) {
            source->stop_capture();
        }
    }

    // PeerConnectionObserver
    void OnSignalingChange(
        webrtc::PeerConnectionInterface::SignalingState) override
    {
    }
    void OnDataChannel(
        webrtc::scoped_refptr<webrtc::DataChannelInterface>) override
    {
    }
    void OnIceGatheringChange(
        webrtc::PeerConnectionInterface::IceGatheringState) override
    {
    }
    void OnIceCandidate(const webrtc::IceCandidate* candidate) override
    {
        {
            std::scoped_lock lock(mutex);
            if (!started) {
                return;
            }
        }
        if (candidate && callbacks.on_candidate) {
            callbacks.on_candidate(
                candidate->ToString(), candidate->sdp_mid());
        }
    }
    void OnConnectionChange(
        webrtc::PeerConnectionInterface::PeerConnectionState state) override
    {
        if (callbacks.on_state) {
            callbacks.on_state(detail::to_public_connection_state(
                static_cast<int>(state)));
        }
    }
    void OnTrack(
        webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
        override
    {
        std::shared_ptr<detail::RemoteScreenSinks> sinks;
        {
            std::scoped_lock lock(mutex);
            if (!started) {
                return;
            }
            sinks = remote_sinks;
        }
        if (!transceiver) {
            return;
        }
        const auto mid = transceiver->mid();
        const auto track = transceiver->receiver()->track();
        if (!mid || !track) {
            return;
        }
        const bool is_video = track->kind()
            == webrtc::MediaStreamTrackInterface::kVideoKind;
        if (is_video && sinks) {
            sinks->attach_video(*mid,
                webrtc::scoped_refptr<webrtc::VideoTrackInterface>(
                    static_cast<webrtc::VideoTrackInterface*>(track.get())));
        } else if (track->kind()
                == webrtc::MediaStreamTrackInterface::kAudioKind
            && sinks) {
            sinks->attach_audio(*mid,
                webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
                    static_cast<webrtc::AudioTrackInterface*>(track.get())));
        } else {
            return;
        }
        if (callbacks.on_remote_track) {
            callbacks.on_remote_track(*mid, track->id(), is_video);
        }
    }
};

GoogleWebRtcScreenSession::GoogleWebRtcScreenSession(
    GoogleWebRtcRuntime& runtime,
    ScreenSessionConfig config,
    ScreenSessionCallbacks callbacks)
    : impl_(std::make_shared<Impl>(
          runtime.impl_, config, std::move(callbacks)))
{
}

GoogleWebRtcScreenSession::~GoogleWebRtcScreenSession()
{
    close();
}

GoogleWebRtcScreenSession::GoogleWebRtcScreenSession(
    GoogleWebRtcScreenSession&&) noexcept = default;
GoogleWebRtcScreenSession& GoogleWebRtcScreenSession::operator=(
    GoogleWebRtcScreenSession&&) noexcept = default;

bool GoogleWebRtcScreenSession::start()
{
    return impl_ && impl_->start();
}

void GoogleWebRtcScreenSession::close() noexcept
{
    if (impl_) {
        impl_->close();
    }
}

void GoogleWebRtcScreenSession::apply_answer(std::string_view sdp)
{
    if (impl_) {
        impl_->apply_answer(sdp);
    }
}

void GoogleWebRtcScreenSession::add_remote_candidate(
    std::string_view candidate,
    std::string_view mid)
{
    if (impl_) {
        impl_->add_remote_candidate(candidate, mid);
    }
}

void GoogleWebRtcScreenSession::set_sharing_enabled(bool enabled)
{
    if (impl_) {
        impl_->set_sharing_enabled(enabled);
    }
}

void GoogleWebRtcScreenSession::set_system_audio_enabled(bool enabled)
{
    if (impl_) {
        impl_->set_system_audio_enabled(enabled);
    }
}

bool GoogleWebRtcScreenSession::sharing_enabled() const noexcept
{
    return impl_ && impl_->sharing_enabled();
}

size_t GoogleWebRtcScreenSession::remote_stream_slots() const noexcept
{
    return impl_ ? impl_->config.remote_stream_slots : 0;
}

bool GoogleWebRtcScreenSession::set_remote_audio_enabled(
    std::string_view mid, bool enabled)
{
    std::shared_ptr<detail::RemoteScreenSinks> sinks;
    if (impl_) {
        std::scoped_lock lock(impl_->mutex);
        sinks = impl_->remote_sinks;
    }
    return sinks && sinks->set_audio_enabled(mid, enabled);
}

bool GoogleWebRtcScreenSession::set_remote_audio_volume(
    std::string_view mid, double volume)
{
    std::shared_ptr<detail::RemoteScreenSinks> sinks;
    if (impl_) {
        std::scoped_lock lock(impl_->mutex);
        sinks = impl_->remote_sinks;
    }
    return sinks && sinks->set_audio_volume(mid, volume);
}

bool GoogleWebRtcScreenSession::get_stats(
    std::function<void(ScreenSessionStats)> callback)
{
    return impl_ && impl_->get_stats(std::move(callback));
}

bool GoogleWebRtcScreenSession::submit_bgra_frame(
    std::span<const uint8_t> bgra,
    int width,
    int height,
    int stride,
    int64_t timestamp_us)
{
    return impl_ && impl_->submit_bgra_frame(bgra, width, height, stride, timestamp_us);
}

std::vector<DesktopCaptureSource> GoogleWebRtcScreenSession::desktop_sources(
    DesktopCaptureKind kind) const
{
    return impl_ ? impl_->desktop_sources(kind)
                 : std::vector<DesktopCaptureSource> { };
}

std::vector<DesktopCaptureSource>
GoogleWebRtcScreenSession::list_desktop_sources(DesktopCaptureKind kind)
{
    auto source = webrtc::make_ref_counted<detail::DesktopVideoSource>(
        detail::DesktopVideoSource::ErrorCallback { });
    return source->sources(kind);
}

DesktopCaptureThumbnail GoogleWebRtcScreenSession::grab_desktop_thumbnail(
    DesktopCaptureKind kind,
    int64_t source_id,
    int max_width,
    int max_height)
{
    return detail::DesktopVideoSource::grab_thumbnail(
        kind, source_id, max_width, max_height);
}

bool GoogleWebRtcScreenSession::start_desktop_capture(
    DesktopCaptureKind kind,
    int64_t source_id,
    int max_fps,
    int max_width,
    int max_height)
{
    return impl_ && impl_->start_desktop_capture(kind, source_id, max_fps, max_width, max_height);
}

void GoogleWebRtcScreenSession::stop_desktop_capture() noexcept
{
    if (impl_) {
        impl_->stop_desktop_capture();
    }
}

} // namespace driscord::media
