#include "system_audio_capture.hpp"

#ifdef __linux__

#include <atomic>
#include <thread>

#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include "log.hpp"

class SystemAudioCaptureLinux : public SystemAudioCapture {
public:
    ~SystemAudioCaptureLinux() override { stop(); }

    bool start(AudioCallback cb) override {
        if (running_) {
            return true;
        }

        callback_ = std::move(cb);

        pa_sample_spec spec{};
        spec.format   = PA_SAMPLE_FLOAT32LE;
        spec.rate     = opus::kSampleRate;
        spec.channels = kChannels;

        constexpr uint32_t kFragFrames = 48; // 20ms @ 48kHz
        constexpr uint32_t kFragBytes  = kFragFrames * kChannels * sizeof(float);

        pa_buffer_attr attr{};
        attr.maxlength = kFragBytes * 4;
        attr.fragsize  = kFragBytes;

        int error = 0;
        pa_       = pa_simple_new(
            nullptr,    // default server
            "driscord", // app name
            PA_STREAM_RECORD,
            "@DEFAULT_MONITOR@", // monitor of default sink
            "screen_audio",
            &spec,
            nullptr, // default channel map
            &attr,
            &error
        );

        if (!pa_) {
            LOG_ERROR() << "pa_simple_new failed: " << pa_strerror(error);
            return false;
        }

        running_ = true;
        thread_  = std::thread([this] { capture_loop(); });
        return true;
    }

    void stop() override {
        if (!running_) {
            return;
        }
        running_ = false;

        if (thread_.joinable()) {
            thread_.join();
        }

        if (pa_) {
            pa_simple_free(pa_);
            pa_ = nullptr;
        }
    }

    bool running() const override { return running_; }

private:
    void capture_loop() {
        constexpr size_t kFramesPerRead = opus::kFrameSize;
        constexpr size_t kBufSize       = kFramesPerRead * kChannels;
        float buf[kBufSize];

        while (running_) {
            int error = 0;
            int ret   = pa_simple_read(pa_, buf, sizeof(buf), &error);
            if (ret < 0) {
                LOG_ERROR() << "pa_simple_read failed: " << pa_strerror(error);
                break;
            }

            if (callback_) {
                callback_(buf, kFramesPerRead, kChannels);
            }
        }
    }

    AudioCallback callback_;
    std::atomic<bool> running_{false};
    pa_simple* pa_ = nullptr;
    std::thread thread_;
};

bool SystemAudioCapture::available() {
    int error = 0;
    pa_sample_spec spec{};
    spec.format   = PA_SAMPLE_FLOAT32LE;
    spec.rate     = opus::kSampleRate;
    spec.channels = 2;

    pa_buffer_attr attr{};
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.fragsize  = 960 * 2 * sizeof(float);

    pa_simple* test = pa_simple_new(
        nullptr,
        "driscord_probe",
        PA_STREAM_RECORD,
        "@DEFAULT_MONITOR@",
        "probe",
        &spec,
        nullptr,
        &attr,
        &error
    );
    if (test) {
        pa_simple_free(test);
        return true;
    }
    return false;
}

std::unique_ptr<SystemAudioCapture> SystemAudioCapture::create() {
    return std::make_unique<SystemAudioCaptureLinux>();
}

std::vector<AudioCaptureTarget> SystemAudioCapture::list_targets() {
    std::vector<AudioCaptureTarget> targets;

    static constexpr auto sink_list_callback =
        [](pa_context* c, const pa_sink_info* i, int is_last, void* userdata) {
            if (!i) {
                LOG_ERROR()
                    << "pa_sink_info_list callback error, pa_sink_info is NULL: "
                    << pa_context_errno(c);
                return;
            }

            if (!userdata) {
                LOG_ERROR() << "pa_sink_info_list callback error: userdata is null";
                return;
            }

            auto* targets = static_cast<std::vector<AudioCaptureTarget>*>(userdata);
            targets->emplace_back(AudioCaptureTarget{i->name, i->description});
        };

    pa_mainloop* ml = pa_mainloop_new();
    if (!ml) {
        LOG_ERROR() << "pa_mainloop_new failed";
        return targets;
    }
    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "DeviceLister");
    if (!ctx) {
        LOG_ERROR() << "pa_context_new failed";
        pa_mainloop_free(ml);
        return targets;
    }

    pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);

    pa_operation* o = pa_context_get_sink_info_list(ctx, sink_list_callback, &targets);

    pa_mainloop_run(ml, NULL);

    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
}

#endif // __linux__
