#include "system_audio_capture.hpp"

#ifdef __linux__

#include <atomic>
#include <thread>

#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include "log.hpp"

// ---------------------------------------------------------------------------
// Custom deleters for PulseAudio resources
// ---------------------------------------------------------------------------
struct PaMainloopDeleter {
    void operator()(pa_mainloop* ml) const noexcept { pa_mainloop_free(ml); }
};
struct PaContextDeleter {
    void operator()(pa_context* ctx) const noexcept
    {
        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
    }
};
struct PaOperationDeleter {
    void operator()(pa_operation* op) const noexcept { pa_operation_unref(op); }
};
struct PaSimpleDeleter {
    void operator()(pa_simple* pa) const noexcept { pa_simple_free(pa); }
};

using PaMainloopPtr = std::unique_ptr<pa_mainloop, PaMainloopDeleter>;
using PaContextPtr = std::unique_ptr<pa_context, PaContextDeleter>;
using PaOperationPtr = std::unique_ptr<pa_operation, PaOperationDeleter>;
using PaSimplePtr = std::unique_ptr<pa_simple, PaSimpleDeleter>;

// ---------------------------------------------------------------------------

class SystemAudioCaptureLinux : public SystemAudioCapture {
public:
    ~SystemAudioCaptureLinux() override { stop(); }

    bool start(AudioCallback cb) override
    {
        if (running_) {
            return true;
        }

        callback_ = std::move(cb);

        pa_sample_spec spec { };
        spec.format = PA_SAMPLE_FLOAT32LE;
        spec.rate = opus::kSampleRate;
        spec.channels = kChannels;

        constexpr uint32_t kFragFrames = 48; // 20ms @ 48kHz
        constexpr uint32_t kFragBytes = kFragFrames * kChannels * sizeof(float);

        pa_buffer_attr attr { };
        attr.maxlength = kFragBytes * 4;
        attr.fragsize = kFragBytes;

        int error = 0;
        pa_.reset(pa_simple_new(nullptr, "driscord", PA_STREAM_RECORD,
            "@DEFAULT_MONITOR@", "screen_audio", &spec, nullptr,
            &attr, &error));

        if (!pa_) {
            LOG_ERROR() << "pa_simple_new failed: " << pa_strerror(error);
            return false;
        }

        running_ = true;
        thread_ = std::thread([this] { capture_loop(); });
        return true;
    }

    void stop() override
    {
        if (!running_) {
            return;
        }
        running_ = false;

        if (thread_.joinable()) {
            thread_.join();
        }

        pa_.reset();
    }

    bool running() const override { return running_; }

private:
    void capture_loop()
    {
        constexpr size_t kFramesPerRead = opus::kFrameSize;
        constexpr size_t kBufSize = kFramesPerRead * kChannels;
        float buf[kBufSize];

        while (running_) {
            int error = 0;
            if (pa_simple_read(pa_.get(), buf, sizeof(buf), &error) < 0) {
                LOG_ERROR() << "pa_simple_read failed: " << pa_strerror(error);
                break;
            }
            if (callback_) {
                callback_(buf, kFramesPerRead, kChannels);
            }
        }
    }

    AudioCallback callback_;
    std::atomic<bool> running_ { false };
    PaSimplePtr pa_;
    std::thread thread_;
};

// ---------------------------------------------------------------------------

bool SystemAudioCapture::available()
{
    pa_sample_spec spec { };
    spec.format = PA_SAMPLE_FLOAT32LE;
    spec.rate = opus::kSampleRate;
    spec.channels = 2;

    pa_buffer_attr attr { };
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.fragsize = 960 * 2 * sizeof(float);

    int error = 0;
    PaSimplePtr test { pa_simple_new(nullptr, "driscord_probe", PA_STREAM_RECORD,
        "@DEFAULT_MONITOR@", "probe", &spec, nullptr,
        &attr, &error) };
    return test != nullptr;
}

std::unique_ptr<SystemAudioCapture> SystemAudioCapture::create()
{
    return std::make_unique<SystemAudioCaptureLinux>();
}

// ---------------------------------------------------------------------------
// Shared helper: creates a mainloop+context, waits for READY, issues one
// PA operation via |issue_op|, runs until it completes.
// Returns false if setup or the operation fails.
// ---------------------------------------------------------------------------
template <typename F>
static bool pa_enumerate(const char* ctx_name, F&& issue_op)
{
    PaMainloopPtr ml { pa_mainloop_new() };
    if (!ml) {
        LOG_ERROR() << "pa_mainloop_new failed";
        return false;
    }

    PaContextPtr ctx { pa_context_new(pa_mainloop_get_api(ml.get()), ctx_name) };
    if (!ctx) {
        LOG_ERROR() << "pa_context_new failed";
        return false;
    }

    if (pa_context_connect(ctx.get(), nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        LOG_ERROR() << "pa_context_connect failed: "
                    << pa_strerror(pa_context_errno(ctx.get()));
        return false;
    }

    while (true) {
        pa_context_state_t state = pa_context_get_state(ctx.get());
        if (state == PA_CONTEXT_READY) {
            break;
        }
        if (!PA_CONTEXT_IS_GOOD(state)) {
            LOG_ERROR() << "pa_context bad state: "
                        << pa_strerror(pa_context_errno(ctx.get()));
            return false;
        }
        pa_mainloop_iterate(ml.get(), 1, nullptr);
    }

    PaOperationPtr op { issue_op(ctx.get()) };
    if (!op) {
        LOG_ERROR() << "PA operation could not be issued";
        return false;
    }

    while (pa_operation_get_state(op.get()) == PA_OPERATION_RUNNING) {
        pa_mainloop_iterate(ml.get(), 1, nullptr);
    }

    return true;
}

// ---------------------------------------------------------------------------

std::vector<AudioCaptureTarget> SystemAudioCapture::list_sinks()
{
    std::vector<AudioCaptureTarget> targets;

    static constexpr auto cb = [](pa_context*, const pa_sink_info* i, int is_last,
                                   void* ud) {
        if (is_last > 0) {
            return;
        }
        if (!i) {
            LOG_ERROR() << "pa_sink_info is NULL (is_last not set)";
            return;
        }
        // Only include sinks that have a monitor source
        if (i->monitor_source == PA_INVALID_INDEX) {
            return;
        }
        static_cast<std::vector<AudioCaptureTarget>*>(ud)->emplace_back(
            AudioCaptureTarget { i->name, i->description });
    };

    pa_enumerate("driscord_sink_lister", [&](pa_context* ctx) {
        return pa_context_get_sink_info_list(ctx, cb, &targets);
    });

    return targets;
}

std::vector<AudioCaptureTarget> SystemAudioCapture::list_sources()
{
    std::vector<AudioCaptureTarget> targets;

    static constexpr auto cb = [](pa_context*, const pa_source_info* i,
                                   int is_last, void* ud) {
        if (is_last > 0) {
            return;
        }
        if (!i) {
            LOG_ERROR() << "pa_source_info is NULL (is_last not set)";
            return;
        }
        // Exclude virtual sink-monitor sources; keep only hardware inputs
        if (i->monitor_of_sink != PA_INVALID_INDEX) {
            return;
        }
        static_cast<std::vector<AudioCaptureTarget>*>(ud)->emplace_back(
            AudioCaptureTarget { i->name, i->description });
    };

    pa_enumerate("driscord_source_lister", [&](pa_context* ctx) {
        return pa_context_get_source_info_list(ctx, cb, &targets);
    });

    return targets;
}

#endif // __linux__
