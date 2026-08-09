#include "webrtc/google_webrtc_voice_session.hpp"

#include "webrtc/google_webrtc_peer_adapters.hpp"
#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_runtime_internal.hpp"
#include "webrtc/google_webrtc_voice_adapters.hpp"

#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_transceiver_interface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace driscord::media {
struct GoogleWebRtcVoiceSession::Impl final
    : webrtc::PeerConnectionObserver,
      std::enable_shared_from_this<Impl> {
    struct RemoteSinkBinding {
        webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
        std::unique_ptr<detail::DecodedAudioSink> sink;
    };

    std::shared_ptr<GoogleWebRtcRuntime::Impl> runtime;
    const VoiceSessionConfig config;
    const VoiceSessionCallbacks callbacks;

    mutable std::mutex mutex;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
    webrtc::scoped_refptr<webrtc::AudioSourceInterface> microphone_source;
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> microphone_track;
    std::vector<RemoteSinkBinding> remote_sinks;
    bool started = false;
    bool start_attempted = false;
    bool close_requested = false;
    bool desired_microphone_enabled;

    Impl(std::shared_ptr<GoogleWebRtcRuntime::Impl> runtime_value,
        VoiceSessionConfig config_value,
        VoiceSessionCallbacks callbacks_value)
        : runtime(std::move(runtime_value))
        , config(config_value)
        , callbacks(std::move(callbacks_value))
        , desired_microphone_enabled(config.microphone_enabled)
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
        if (config.remote_track_slots > kMaxRemoteTrackSlots) {
            report_error("voice receive slot count exceeds the safety limit");
            return false;
        }
        if (config.max_microphone_bitrate_bps <= 0
            || config.max_microphone_bitrate_bps > 510'000) {
            report_error("voice microphone bitrate is outside the Opus range");
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
                "voice PeerConnection creation failed", pc_result.error()));
            return false;
        }
        auto new_pc = pc_result.MoveValue();

        webrtc::AudioOptions audio_options;
        auto new_source = runtime->factory->CreateAudioSource(audio_options);
        if (!new_source) {
            report_error("voice microphone source creation failed");
            new_pc->Close();
            return false;
        }
        auto new_track = runtime->factory->CreateAudioTrack(
            "driscord-microphone", new_source.get());
        if (!new_track) {
            report_error("voice microphone track creation failed");
            new_pc->Close();
            return false;
        }
        bool microphone_enabled = false;
        {
            std::scoped_lock lock(mutex);
            microphone_enabled = desired_microphone_enabled;
        }
        new_track->set_enabled(microphone_enabled);

        webrtc::RtpTransceiverInit send_init;
        send_init.direction = webrtc::RtpTransceiverDirection::kSendOnly;
        send_init.stream_ids = { "driscord-voice" };
        webrtc::RtpEncodingParameters send_encoding;
        send_encoding.max_bitrate_bps = config.max_microphone_bitrate_bps;
        send_init.send_encodings.push_back(std::move(send_encoding));
        auto sender = new_pc->AddTransceiver(new_track, send_init);
        if (!sender.ok()) {
            report_error(detail::rtc_error_message(
                "voice microphone transceiver creation failed", sender.error()));
            new_pc->Close();
            return false;
        }

        webrtc::RtpTransceiverInit receive_init;
        receive_init.direction = webrtc::RtpTransceiverDirection::kRecvOnly;
        for (size_t i = 0; i < config.remote_track_slots; ++i) {
            auto receiver = new_pc->AddTransceiver(
                webrtc::MediaType::AUDIO, receive_init);
            if (!receiver.ok()) {
                report_error(detail::rtc_error_message(
                    "voice receive transceiver creation failed", receiver.error()));
                new_pc->Close();
                return false;
            }
        }

        bool cancelled = false;
        {
            std::scoped_lock lock(mutex);
            // A concurrent close() wins over a partially constructed start().
            if (close_requested) {
                cancelled = true;
            } else {
                pc = new_pc;
                microphone_source = std::move(new_source);
                microphone_track = std::move(new_track);
                started = true;
            }
        }
        if (cancelled) {
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
        std::vector<RemoteSinkBinding> old_sinks;
        {
            std::scoped_lock lock(mutex);
            old_pc = std::move(pc);
            microphone_track = nullptr;
            microphone_source = nullptr;
            old_sinks = std::move(remote_sinks);
            started = false;
            close_requested = true;
        }
        for (const auto& binding : old_sinks) {
            if (binding.track && binding.sink) {
                binding.track->RemoveSink(binding.sink.get());
            }
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

    bool owns(const webrtc::scoped_refptr<webrtc::PeerConnectionInterface>& other) const
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
            report_error(detail::rtc_error_message(
                "setting voice local description failed", error));
            return;
        }

        const auto* description = expected->local_description();
        std::string sdp;
        if (!description || !description->ToString(&sdp)) {
            report_error("serializing voice local description failed");
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
            report_error(detail::rtc_error_message(
                "setting voice remote answer failed", error));
        }
    }

    void apply_answer(std::string_view sdp)
    {
        auto current = current_pc();
        if (!current) {
            report_error("voice answer received before session start");
            return;
        }

        webrtc::SdpParseError parse_error;
        auto description = webrtc::CreateSessionDescription(
            webrtc::SdpType::kAnswer, sdp, &parse_error);
        if (!description) {
            report_error("invalid voice answer SDP: " + parse_error.description);
            return;
        }
        auto observer = webrtc::make_ref_counted<detail::SetRemoteDescriptionObserver>(
            [weak = weak_from_this(), current](webrtc::RTCError error) {
                if (auto self = weak.lock()) {
                    self->on_remote_description(
                        current, std::move(error));
                }
            });
        current->SetRemoteDescription(std::move(description), std::move(observer));
    }

    void add_remote_candidate(std::string_view candidate, std::string_view mid)
    {
        auto current = current_pc();
        if (!current) {
            report_error("voice ICE candidate received before session start");
            return;
        }

        webrtc::SdpParseError parse_error;
        auto parsed = std::unique_ptr<webrtc::IceCandidate>(
            webrtc::CreateIceCandidate(
                mid, -1, std::string(candidate), &parse_error));
        if (!parsed) {
            report_error("invalid voice ICE candidate: " + parse_error.description);
            return;
        }
        const std::weak_ptr<Impl> weak = weak_from_this();
        current->AddIceCandidate(std::move(parsed),
            [weak, current](webrtc::RTCError error) {
                auto self = weak.lock();
                if (self && self->owns(current) && !error.ok()) {
                    self->report_error(
                        detail::rtc_error_message(
                            "adding voice ICE candidate failed", error));
                }
            });
    }

    void set_microphone_enabled(bool enabled)
    {
        webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
        {
            std::scoped_lock lock(mutex);
            desired_microphone_enabled = enabled;
            track = microphone_track;
        }
        if (track) {
            track->set_enabled(enabled);
        }
    }

    bool microphone_enabled() const noexcept
    {
        webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
        bool desired = false;
        {
            std::scoped_lock lock(mutex);
            track = microphone_track;
            desired = desired_microphone_enabled;
        }
        return track ? track->enabled() : desired;
    }

    webrtc::scoped_refptr<webrtc::AudioTrackInterface> remote_audio_track(
        std::string_view mid) const
    {
        auto current = current_pc();
        if (!current) {
            return nullptr;
        }
        for (const auto& transceiver : current->GetTransceivers()) {
            const auto transceiver_mid = transceiver->mid();
            if (!transceiver_mid || *transceiver_mid != mid
                || transceiver->media_type() != webrtc::MediaType::AUDIO) {
                continue;
            }
            auto track = transceiver->receiver()->track();
            if (!track || track->kind() != webrtc::MediaStreamTrackInterface::kAudioKind) {
                return nullptr;
            }
            return webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
                static_cast<webrtc::AudioTrackInterface*>(track.get()));
        }
        return nullptr;
    }

    void deliver_remote_audio(std::string_view mid,
        const void* audio_data,
        int bits_per_sample,
        int sample_rate,
        size_t channels,
        size_t frames) const
    {
        if (!callbacks.on_remote_audio || !audio_data || bits_per_sample != 16
            || sample_rate <= 0 || channels == 0 || frames == 0
            || frames > std::numeric_limits<size_t>::max() / channels) {
            return;
        }
        {
            std::scoped_lock lock(mutex);
            if (!started) {
                return;
            }
        }
        callbacks.on_remote_audio(mid,
            DecodedAudioFrameView {
                .samples = std::span<const int16_t>(
                    static_cast<const int16_t*>(audio_data), frames * channels),
                .sample_rate_hz = sample_rate,
                .channels = channels,
                .frames = frames,
            });
    }

    void attach_remote_audio_sink(std::string mid,
        webrtc::scoped_refptr<webrtc::AudioTrackInterface> track)
    {
        if (!callbacks.on_remote_audio) {
            return;
        }

        const std::weak_ptr<Impl> weak = weak_from_this();
        RemoteSinkBinding binding {
            .track = std::move(track),
            .sink = std::make_unique<detail::DecodedAudioSink>(mid,
                [weak](std::string_view sink_mid,
                    const void* audio_data,
                    int bits_per_sample,
                    int sample_rate,
                    size_t channels,
                    size_t frames) {
                    if (auto self = weak.lock()) {
                        self->deliver_remote_audio(sink_mid, audio_data,
                            bits_per_sample, sample_rate, channels, frames);
                    }
                }),
        };
        binding.track->AddSink(binding.sink.get());

        bool retained = false;
        {
            std::scoped_lock lock(mutex);
            const auto duplicate = std::find_if(remote_sinks.begin(),
                remote_sinks.end(), [&mid](const RemoteSinkBinding& candidate) {
                    return candidate.sink->mid() == mid;
                });
            if (started && duplicate == remote_sinks.end()) {
                remote_sinks.push_back(std::move(binding));
                retained = true;
            }
        }
        if (!retained && binding.track && binding.sink) {
            binding.track->RemoveSink(binding.sink.get());
        }
    }

    bool get_stats(std::function<void(VoiceSessionStats)> callback)
    {
        auto current = current_pc();
        if (!current || !callback) {
            return false;
        }
        const std::weak_ptr<Impl> weak = weak_from_this();
        auto observer = webrtc::make_ref_counted<detail::StatsObserver>(
            [weak, current, callback = std::move(callback)](
                const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
                if (auto self = weak.lock(); self && self->owns(current) && report) {
                    callback(detail::parse_voice_stats(*report));
                }
            });
        current->GetStats(observer.get());
        return true;
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
        if (!candidate || !callbacks.on_candidate) {
            return;
        }
        callbacks.on_candidate(candidate->ToString(), candidate->sdp_mid());
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
        webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override
    {
        {
            std::scoped_lock lock(mutex);
            if (!started) {
                return;
            }
        }
        if (!transceiver) {
            return;
        }
        const auto mid = transceiver->mid();
        const auto track = transceiver->receiver()->track();
        if (mid && track
            && track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
            auto audio_track = webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
                static_cast<webrtc::AudioTrackInterface*>(track.get()));
            attach_remote_audio_sink(*mid, std::move(audio_track));
            if (callbacks.on_remote_track) {
                callbacks.on_remote_track(*mid, track->id());
            }
        }
    }
};

GoogleWebRtcVoiceSession::GoogleWebRtcVoiceSession(
    GoogleWebRtcRuntime& runtime,
    VoiceSessionConfig config,
    VoiceSessionCallbacks callbacks)
    : impl_(std::make_shared<Impl>(
          runtime.impl_, config, std::move(callbacks)))
{
}

GoogleWebRtcVoiceSession::~GoogleWebRtcVoiceSession()
{
    close();
}

GoogleWebRtcVoiceSession::GoogleWebRtcVoiceSession(
    GoogleWebRtcVoiceSession&&) noexcept = default;
GoogleWebRtcVoiceSession& GoogleWebRtcVoiceSession::operator=(
    GoogleWebRtcVoiceSession&&) noexcept = default;

bool GoogleWebRtcVoiceSession::start()
{
    return impl_ && impl_->start();
}

void GoogleWebRtcVoiceSession::close() noexcept
{
    if (impl_) {
        impl_->close();
    }
}

void GoogleWebRtcVoiceSession::apply_answer(std::string_view sdp)
{
    if (impl_) {
        impl_->apply_answer(sdp);
    }
}

void GoogleWebRtcVoiceSession::add_remote_candidate(
    std::string_view candidate,
    std::string_view mid)
{
    if (impl_) {
        impl_->add_remote_candidate(candidate, mid);
    }
}

void GoogleWebRtcVoiceSession::set_microphone_enabled(bool enabled)
{
    if (impl_) {
        impl_->set_microphone_enabled(enabled);
    }
}

bool GoogleWebRtcVoiceSession::microphone_enabled() const noexcept
{
    return impl_ && impl_->microphone_enabled();
}

size_t GoogleWebRtcVoiceSession::remote_track_slots() const noexcept
{
    return impl_ ? impl_->config.remote_track_slots : 0;
}

bool GoogleWebRtcVoiceSession::set_remote_track_enabled(
    std::string_view mid,
    bool enabled)
{
    if (!impl_) {
        return false;
    }
    auto track = impl_->remote_audio_track(mid);
    return track && track->set_enabled(enabled);
}

bool GoogleWebRtcVoiceSession::set_remote_track_volume(
    std::string_view mid,
    double volume)
{
    if (!impl_ || !std::isfinite(volume) || volume < 0.0 || volume > 10.0) {
        return false;
    }
    auto track = impl_->remote_audio_track(mid);
    auto runtime = impl_->runtime;
    if (!track || !runtime || !runtime->signaling_thread) {
        return false;
    }
    return runtime->signaling_thread->BlockingCall(
        [track = std::move(track), volume] {
            auto source = track->GetSource();
            if (!source) {
                return false;
            }
            source->SetVolume(volume);
            return true;
        });
}

bool GoogleWebRtcVoiceSession::get_stats(
    std::function<void(VoiceSessionStats)> callback)
{
    return impl_ && impl_->get_stats(std::move(callback));
}

} // namespace driscord::media
