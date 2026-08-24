#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_audio_devices.hpp"
#include "webrtc/google_webrtc_runtime_internal.hpp"

#include "api/audio/create_audio_device_module.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media_with_defaults.h"
#include "api/environment/environment_factory.h"
#include "modules/audio_device/include/test_audio_device.h"
#include "rtc_base/buffer.h"
#include "rtc_base/ssl_adapter.h"

#include "utils/log.hpp"

#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace driscord::media {

class InjectedPcmSource final {
public:
    explicit InjectedPcmSource(const InjectedAudioDeviceConfig& config)
        : sample_rate_hz_(config.sample_rate_hz)
        , channels_(config.channels)
        , samples_per_frame_(
              static_cast<size_t>(config.sample_rate_hz / 100) * config.channels)
        , max_buffered_frames_(config.max_buffered_frames)
    {
    }

    [[nodiscard]] int sample_rate_hz() const noexcept { return sample_rate_hz_; }
    [[nodiscard]] int channels() const noexcept
    {
        return static_cast<int>(channels_);
    }
    [[nodiscard]] size_t samples_per_frame() const noexcept
    {
        return samples_per_frame_;
    }

    [[nodiscard]] bool submit(std::span<const int16_t> samples)
    {
        if (samples.size() != samples_per_frame_) {
            return false;
        }
        std::scoped_lock lock(mutex_);
        if (frames_.size() >= max_buffered_frames_) {
            return false;
        }
        frames_.emplace_back(samples.begin(), samples.end());
        return true;
    }

    bool next_frame(std::vector<int16_t>& frame)
    {
        std::scoped_lock lock(mutex_);
        if (frames_.empty()) {
            return false;
        }
        frame = std::move(frames_.front());
        frames_.pop_front();
        return true;
    }

private:
    const int sample_rate_hz_;
    const size_t channels_;
    const size_t samples_per_frame_;
    const size_t max_buffered_frames_;
    std::mutex mutex_;
    std::deque<std::vector<int16_t>> frames_;
};

namespace {
    bool is_supported_sample_rate(int sample_rate_hz)
    {
        switch (sample_rate_hz) {
        case 8'000:
        case 16'000:
        case 24'000:
        case 32'000:
        case 44'100:
        case 48'000:
            return true;
        default:
            return false;
        }
    }

    void validate(const InjectedAudioDeviceConfig& config)
    {
        if (!is_supported_sample_rate(config.sample_rate_hz)) {
            throw std::invalid_argument("unsupported injected audio sample rate");
        }
        if (config.channels == 0 || config.channels > 2) {
            throw std::invalid_argument("injected audio must have one or two channels");
        }
        if (config.max_buffered_frames == 0) {
            throw std::invalid_argument("injected audio queue must not be empty");
        }
    }

    class QueuedPcmCapturer final
        : public webrtc::TestAudioDeviceModule::Capturer {
    public:
        explicit QueuedPcmCapturer(std::shared_ptr<InjectedPcmSource> source)
            : source_(std::move(source))
            , silence_(source_->samples_per_frame(), 0)
        {
        }

        int SamplingFrequency() const override { return source_->sample_rate_hz(); }
        int NumChannels() const override { return source_->channels(); }

        bool Capture(webrtc::BufferT<int16_t>* buffer) override
        {
            if (source_->next_frame(frame_)) {
                buffer->SetData(frame_);
            } else {
                buffer->SetData(silence_);
            }
            return true;
        }

    private:
        std::shared_ptr<InjectedPcmSource> source_;
        const std::vector<int16_t> silence_;
        std::vector<int16_t> frame_;
    };

    class CallbackPcmRenderer final
        : public webrtc::TestAudioDeviceModule::Renderer {
    public:
        explicit CallbackPcmRenderer(const InjectedAudioDeviceConfig& config)
            : sample_rate_hz_(config.sample_rate_hz)
            , channels_(static_cast<int>(config.channels))
            , callback_(config.on_rendered_audio)
        {
        }

        int SamplingFrequency() const override { return sample_rate_hz_; }
        int NumChannels() const override { return channels_; }

        bool Render(std::span<const int16_t> samples) override
        {
            if (callback_) {
                callback_(samples, sample_rate_hz_,
                    static_cast<size_t>(channels_));
            }
            return true;
        }

    private:
        const int sample_rate_hz_;
        const int channels_;
        const InjectedAudioDeviceConfig::RenderCallback callback_;
    };

    class SslRuntime final {
    public:
        SslRuntime()
        {
            if (!webrtc::InitializeSSL()) {
                throw std::runtime_error("Google WebRTC SSL initialization failed");
            }
        }

        ~SslRuntime() { webrtc::CleanupSSL(); }
    };

    SslRuntime& ssl_runtime()
    {
        static SslRuntime instance;
        return instance;
    }

    std::unique_ptr<webrtc::Thread> make_thread(const char* name, bool with_socket_server)
    {
        auto thread = with_socket_server ? webrtc::Thread::CreateWithSocketServer()
                                         : webrtc::Thread::Create();
        if (!thread || !thread->SetName(name, nullptr) || !thread->Start()) {
            throw std::runtime_error(std::string("Failed to start WebRTC thread: ") + name);
        }
        return thread;
    }

} // namespace

GoogleWebRtcRuntime::Impl::Impl(const GoogleWebRtcRuntimeConfig& config)
    : network_thread(make_thread("driscord-webrtc-network", true))
    , worker_thread(make_thread("driscord-webrtc-worker", false))
    , signaling_thread(make_thread("driscord-webrtc-signaling", false))
    , ice_servers(config.ice_servers)
{
    (void)ssl_runtime();

    webrtc::PeerConnectionFactoryDependencies dependencies;
    dependencies.network_thread = network_thread.get();
    dependencies.worker_thread = worker_thread.get();
    dependencies.signaling_thread = signaling_thread.get();
    dependencies.socket_factory = network_thread->socketserver();
    dependencies.env = webrtc::CreateEnvironment();

    if (config.injected_audio_device) {
        validate(*config.injected_audio_device);
        injected_pcm_source = std::make_shared<InjectedPcmSource>(*config.injected_audio_device);
        auto renderer = config.injected_audio_device->on_rendered_audio
            ? std::unique_ptr<webrtc::TestAudioDeviceModule::Renderer>(
                  std::make_unique<CallbackPcmRenderer>(
                      *config.injected_audio_device))
            : webrtc::TestAudioDeviceModule::CreateDiscardRenderer(
                  config.injected_audio_device->sample_rate_hz,
                  static_cast<int>(config.injected_audio_device->channels));
        dependencies.adm = webrtc::TestAudioDeviceModule::Create(
            *dependencies.env,
            std::make_unique<QueuedPcmCapturer>(injected_pcm_source),
            std::move(renderer));
        if (!dependencies.adm) {
            throw std::runtime_error(
                "Google WebRTC injected audio device creation failed");
        }
        audio_device = dependencies.adm;
    } else {
        // Create the platform ADM on WebRTC's worker thread: its Linux backend
        // is sequence-bound, and the voice engine also controls it there.
        audio_device = worker_thread->BlockingCall([&dependencies] {
            return webrtc::CreateAudioDeviceModule(
                *dependencies.env,
                webrtc::AudioDeviceModule::kPlatformDefaultAudio);
        });
        if (!audio_device) {
            throw std::runtime_error(
                "Google WebRTC platform audio device creation failed");
        }
        // Probe Init() here, on the same worker thread, before the ADM
        // reaches the voice engine: adm_helpers.cc wraps this very call in a
        // fatal RTC_CHECK, so on a machine without a working audio server the
        // first session start would abort the whole process. Init() is
        // idempotent — the engine's later call just returns 0 again.
        const auto init_result = worker_thread->BlockingCall(
            [this] { return audio_device->Init(); });
        if (init_result != 0) {
            LOG_WARNING() << "Platform audio device failed to initialise (rc="
                          << init_result
                          << "); falling back to the silent dummy device — "
                             "sessions still connect, but nothing is captured "
                             "or played out";
            audio_device = worker_thread->BlockingCall([&dependencies] {
                return webrtc::CreateAudioDeviceModule(*dependencies.env,
                    webrtc::AudioDeviceModule::kDummyAudio);
            });
            if (!audio_device) {
                throw std::runtime_error(
                    "Google WebRTC dummy audio device creation failed");
            }
            audio_device_degraded = true;
        }
        dependencies.adm = audio_device;
    }
    webrtc::EnableMediaWithDefaults(dependencies);

    factory = webrtc::CreateModularPeerConnectionFactory(std::move(dependencies));
    if (!factory) {
        throw std::runtime_error("Google WebRTC PeerConnectionFactory creation failed");
    }
}

bool GoogleWebRtcRuntime::Impl::submit_recorded_audio_10ms(
    std::span<const int16_t> samples)
{
    return injected_pcm_source && injected_pcm_source->submit(samples);
}

std::vector<AudioDeviceInfo> GoogleWebRtcRuntime::Impl::audio_devices(
    bool recording) const
{
    if (!audio_device || injected_pcm_source || !worker_thread) {
        return { };
    }
    return worker_thread->BlockingCall([this, recording] {
        return detail::list_audio_devices(*audio_device, recording);
    });
}

bool GoogleWebRtcRuntime::Impl::set_audio_device(
    bool recording, std::string_view id)
{
    if (!audio_device || injected_pcm_source || !worker_thread || id.empty()) {
        return false;
    }
    const std::string requested_id(id);
    return worker_thread->BlockingCall([this, recording, &requested_id] {
        return detail::select_audio_device(
            *audio_device, recording, requested_id);
    });
}

GoogleWebRtcRuntime::Impl::~Impl()
{
    // Factory must be released while all three threads are still running.
    factory = nullptr;
    if (worker_thread) {
        worker_thread->BlockingCall([this] { audio_device = nullptr; });
    } else {
        audio_device = nullptr;
    }
    signaling_thread->Stop();
    worker_thread->Stop();
    network_thread->Stop();
}

GoogleWebRtcRuntime::GoogleWebRtcRuntime()
    : GoogleWebRtcRuntime(GoogleWebRtcRuntimeConfig { })
{
}

GoogleWebRtcRuntime::GoogleWebRtcRuntime(GoogleWebRtcRuntimeConfig config)
    : impl_(std::make_shared<Impl>(config))
{
}

GoogleWebRtcRuntime::~GoogleWebRtcRuntime() = default;
GoogleWebRtcRuntime::GoogleWebRtcRuntime(GoogleWebRtcRuntime&&) noexcept = default;
GoogleWebRtcRuntime& GoogleWebRtcRuntime::operator=(GoogleWebRtcRuntime&&) noexcept = default;

bool GoogleWebRtcRuntime::ready() const noexcept
{
    return impl_ && impl_->factory;
}

bool GoogleWebRtcRuntime::audio_device_degraded() const noexcept
{
    return impl_ && impl_->audio_device_degraded;
}

bool GoogleWebRtcRuntime::submit_recorded_audio_10ms(
    std::span<const int16_t> interleaved_samples)
{
    return impl_ && impl_->submit_recorded_audio_10ms(interleaved_samples);
}

std::vector<AudioDeviceInfo> GoogleWebRtcRuntime::recording_devices() const
{
    return impl_ ? impl_->audio_devices(true)
                 : std::vector<AudioDeviceInfo> { };
}

std::vector<AudioDeviceInfo> GoogleWebRtcRuntime::playout_devices() const
{
    return impl_ ? impl_->audio_devices(false)
                 : std::vector<AudioDeviceInfo> { };
}

bool GoogleWebRtcRuntime::set_recording_device(std::string_view id)
{
    return impl_ && impl_->set_audio_device(true, id);
}

bool GoogleWebRtcRuntime::set_playout_device(std::string_view id)
{
    return impl_ && impl_->set_audio_device(false, id);
}

} // namespace driscord::media
