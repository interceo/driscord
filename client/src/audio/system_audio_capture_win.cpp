#include "system_audio_capture.hpp"

#ifdef _WIN32

#include <atomic>
#include <thread>

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include "log.hpp"

class SystemAudioCaptureWin : public SystemAudioCapture {
public:
    ~SystemAudioCaptureWin() override { stop(); }

    bool start(AudioCallback cb) override {
        if (running_) {
            return true;
        }

        callback_ = std::move(cb);

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        co_initialized_ = SUCCEEDED(hr) || hr == S_FALSE;

        IMMDeviceEnumerator* enumerator = nullptr;
        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator)
        );
        if (FAILED(hr)) {
            LOG_ERROR() << "CoCreateInstance(MMDeviceEnumerator) failed: " << std::hex << hr;
            return false;
        }

        IMMDevice* device = nullptr;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        enumerator->Release();
        if (FAILED(hr)) {
            LOG_ERROR() << "GetDefaultAudioEndpoint failed: " << std::hex << hr;
            return false;
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audio_client_));
        device->Release();
        if (FAILED(hr)) {
            LOG_ERROR() << "Activate(IAudioClient) failed: " << std::hex << hr;
            return false;
        }

        WAVEFORMATEX* mix_format = nullptr;
        hr = audio_client_->GetMixFormat(&mix_format);
        if (FAILED(hr)) {
            LOG_ERROR() << "GetMixFormat failed: " << std::hex << hr;
            return false;
        }

        WAVEFORMATEX desired{};
        desired.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        desired.nChannels = CHANNELS;
        desired.nSamplesPerSec = SAMPLE_RATE;
        desired.wBitsPerSample = 32;
        desired.nBlockAlign = desired.nChannels * desired.wBitsPerSample / 8;
        desired.nAvgBytesPerSec = desired.nSamplesPerSec * desired.nBlockAlign;

        REFERENCE_TIME duration = 200000;  // 20ms
        hr = audio_client_->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            duration,
            0,
            &desired,
            nullptr
        );
        CoTaskMemFree(mix_format);
        if (FAILED(hr)) {
            LOG_ERROR() << "IAudioClient::Initialize (loopback) failed: " << std::hex << hr;
            return false;
        }

        hr = audio_client_->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture_client_));
        if (FAILED(hr)) {
            LOG_ERROR() << "GetService(IAudioCaptureClient) failed: " << std::hex << hr;
            return false;
        }

        hr = audio_client_->Start();
        if (FAILED(hr)) {
            LOG_ERROR() << "IAudioClient::Start failed: " << std::hex << hr;
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

        if (audio_client_) {
            audio_client_->Stop();
        }
        if (capture_client_) {
            capture_client_->Release();
            capture_client_ = nullptr;
        }
        if (audio_client_) {
            audio_client_->Release();
            audio_client_ = nullptr;
        }
        if (co_initialized_) {
            CoUninitialize();
            co_initialized_ = false;
        }
    }

    bool running() const override { return running_; }

private:
    void capture_loop() {
        while (running_) {
            UINT32 packet_length = 0;
            HRESULT hr = capture_client_->GetNextPacketSize(&packet_length);
            if (FAILED(hr)) {
                break;
            }

            while (packet_length > 0) {
                BYTE* data = nullptr;
                UINT32 num_frames = 0;
                DWORD flags = 0;

                hr = capture_client_->GetBuffer(&data, &num_frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) {
                    break;
                }

                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && callback_ && num_frames > 0) {
                    callback_(reinterpret_cast<const float*>(data), static_cast<size_t>(num_frames), CHANNELS);
                }

                capture_client_->ReleaseBuffer(num_frames);

                hr = capture_client_->GetNextPacketSize(&packet_length);
                if (FAILED(hr)) {
                    break;
                }
            }

            Sleep(5);
        }
    }

    AudioCallback callback_;
    std::atomic<bool> running_{false};
    bool co_initialized_ = false;
    IAudioClient* audio_client_ = nullptr;
    IAudioCaptureClient* capture_client_ = nullptr;
    std::thread thread_;
};

bool SystemAudioCapture::available() { return true; }

std::unique_ptr<SystemAudioCapture> SystemAudioCapture::create() { return std::make_unique<SystemAudioCaptureWin>(); }

#endif  // _WIN32
