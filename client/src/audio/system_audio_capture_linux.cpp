#include "system_audio_capture.hpp"

#ifdef __linux__

#include <atomic>
#include <thread>

#include <pulse/error.h>
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
        spec.format = PA_SAMPLE_FLOAT32LE;
        spec.rate = SAMPLE_RATE;
        spec.channels = CHANNELS;

        int error = 0;
        pa_ = pa_simple_new(
            nullptr,     // default server
            "driscord",  // app name
            PA_STREAM_RECORD,
            "@DEFAULT_MONITOR@",  // monitor of default sink
            "screen_audio",
            &spec,
            nullptr,  // default channel map
            nullptr,  // default buffering
            &error
        );

        if (!pa_) {
            LOG_ERROR() << "pa_simple_new failed: " << pa_strerror(error);
            return false;
        }

        running_ = true;
        thread_ = std::thread([this] { capture_loop(); });
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
        constexpr size_t kFramesPerRead = 960;  // 20ms at 48kHz
        constexpr size_t kBufSize = kFramesPerRead * CHANNELS;
        float buf[kBufSize];

        while (running_) {
            int error = 0;
            int ret = pa_simple_read(pa_, buf, sizeof(buf), &error);
            if (ret < 0) {
                LOG_ERROR() << "pa_simple_read failed: " << pa_strerror(error);
                break;
            }

            if (callback_) {
                callback_(buf, kFramesPerRead, CHANNELS);
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
    spec.format = PA_SAMPLE_FLOAT32LE;
    spec.rate = 48000;
    spec.channels = 2;

    pa_simple* test = pa_simple_new(
        nullptr,
        "driscord_probe",
        PA_STREAM_RECORD,
        "@DEFAULT_MONITOR@",
        "probe",
        &spec,
        nullptr,
        nullptr,
        &error
    );
    if (test) {
        pa_simple_free(test);
        return true;
    }
    return false;
}

std::unique_ptr<SystemAudioCapture> SystemAudioCapture::create() { return std::make_unique<SystemAudioCaptureLinux>(); }

#endif  // __linux__
