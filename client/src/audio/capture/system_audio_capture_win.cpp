#include "system_audio_capture.hpp"

#ifdef _WIN32

// clang-format off
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
// clang-format on

#if __has_include(<audioclientactivationparams.h>)
#include <audioclientactivationparams.h>
#define DRIST_HAS_PROCESS_LOOPBACK 1
#else
#define DRIST_HAS_PROCESS_LOOPBACK 0
#endif

#include <atomic>
#include <thread>

#include "log.hpp"

#if DRIST_HAS_PROCESS_LOOPBACK

class LoopbackActivationHandler : public IActivateAudioInterfaceCompletionHandler {
public:
    LoopbackActivationHandler()
        : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
    ~LoopbackActivationHandler() {
        if (event_) {
            CloseHandle(event_);
        }
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_count_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = InterlockedDecrement(&ref_count_);
        if (c == 0) {
            delete this;
        }
        return c;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        op->GetActivateResult(&activate_hr_, &activated_);
        SetEvent(event_);
        return S_OK;
    }

    bool wait(DWORD ms = 5000) { return WaitForSingleObject(event_, ms) == WAIT_OBJECT_0; }
    HRESULT result() const { return activate_hr_; }
    IUnknown* interface_ptr() const { return activated_; }

private:
    ULONG ref_count_     = 1;
    HANDLE event_        = nullptr;
    HRESULT activate_hr_ = E_FAIL;
    IUnknown* activated_ = nullptr;
};

#endif // DRIST_HAS_PROCESS_LOOPBACK

class SystemAudioCaptureWin : public SystemAudioCapture {
public:
    ~SystemAudioCaptureWin() override { stop(); }

    bool start(AudioCallback cb) override {
        if (running_) {
            return true;
        }

        callback_ = std::move(cb);

        HRESULT hr      = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        co_initialized_ = SUCCEEDED(hr) || hr == S_FALSE;

        bool initialized = false;

#if DRIST_HAS_PROCESS_LOOPBACK
        initialized = try_process_loopback();
        if (initialized) {
            LOG_INFO() << "system audio: using process loopback exclusion (self excluded)";
        }
#endif

        if (!initialized) {
            if (!init_standard_loopback()) {
                return false;
            }
            LOG_INFO() << "system audio: using standard WASAPI loopback";
        }

        hr = audio_client_->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture_client_));
        if (FAILED(hr)) {
            LOG_ERROR() << "GetService(IAudioCaptureClient) failed: " << std::hex << hr;
            cleanup_client();
            return false;
        }

        hr = audio_client_->Start();
        if (FAILED(hr)) {
            LOG_ERROR() << "IAudioClient::Start failed: " << std::hex << hr;
            cleanup_client();
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

        cleanup_client();

        if (co_initialized_) {
            CoUninitialize();
            co_initialized_ = false;
        }
    }

    bool running() const override { return running_; }

private:
#if DRIST_HAS_PROCESS_LOOPBACK
    bool try_process_loopback() {
        AUDIOCLIENT_ACTIVATION_PARAMS params{};
        params.ActivationType                            = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId     = GetCurrentProcessId();
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;

        PROPVARIANT pv{};
        pv.vt             = VT_BLOB;
        pv.blob.cbSize    = sizeof(params);
        pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

        auto* handler                             = new LoopbackActivationHandler();
        IActivateAudioInterfaceAsyncOperation* op = nullptr;

        HRESULT hr = ActivateAudioInterfaceAsync(
            VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
            __uuidof(IAudioClient),
            &pv,
            handler,
            &op
        );

        if (FAILED(hr)) {
            LOG_INFO() << "ActivateAudioInterfaceAsync unavailable: " << std::hex << hr;
            handler->Release();
            return false;
        }

        if (!handler->wait()) {
            LOG_ERROR() << "process loopback activation timed out";
            handler->Release();
            if (op) {
                op->Release();
            }
            return false;
        }

        if (FAILED(handler->result())) {
            LOG_INFO() << "process loopback activation failed: " << std::hex << handler->result();
            handler->Release();
            if (op) {
                op->Release();
            }
            return false;
        }

        IUnknown* iface = handler->interface_ptr();
        hr              = iface->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&audio_client_));
        handler->Release();
        if (op) {
            op->Release();
        }

        if (FAILED(hr) || !audio_client_) {
            return false;
        }

        return init_client(0);
    }
#endif

    bool init_standard_loopback() {
        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr                      = CoCreateInstance(
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
        hr                = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
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

        return init_client(AUDCLNT_STREAMFLAGS_LOOPBACK);
    }

    bool init_client(DWORD extra_flags) {
        WAVEFORMATEX desired{};
        desired.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
        desired.nChannels       = kChannels;
        desired.nSamplesPerSec  = opus::kSampleRate;
        desired.wBitsPerSample  = 32;
        desired.nBlockAlign     = desired.nChannels * desired.wBitsPerSample / 8;
        desired.nAvgBytesPerSec = desired.nSamplesPerSec * desired.nBlockAlign;

        REFERENCE_TIME duration = 200000; // 20ms
        HRESULT hr              = audio_client_->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            extra_flags | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            duration,
            0,
            &desired,
            nullptr
        );
        if (FAILED(hr)) {
            LOG_ERROR() << "IAudioClient::Initialize failed: " << std::hex << hr;
            audio_client_->Release();
            audio_client_ = nullptr;
            return false;
        }
        return true;
    }

    void cleanup_client() {
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
    }

    void capture_loop() {
        while (running_) {
            UINT32 packet_length = 0;
            HRESULT hr           = capture_client_->GetNextPacketSize(&packet_length);
            if (FAILED(hr)) {
                break;
            }

            while (packet_length > 0) {
                BYTE* data        = nullptr;
                UINT32 num_frames = 0;
                DWORD flags       = 0;

                hr = capture_client_->GetBuffer(&data, &num_frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) {
                    break;
                }

                if (callback_ && num_frames > 0) {
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        silence_buf_.assign(static_cast<size_t>(num_frames) * kChannels, 0.0f);
                        callback_(silence_buf_.data(), static_cast<size_t>(num_frames), kChannels);
                        ++silent_count_;
                    } else {
                        if (first_audio_logged_ == 0) {
                            first_audio_logged_ = 1;
                            LOG_INFO()
                                << "system audio: first non-silent buffer after " << silent_count_ << " silent buffers";
                        }
                        callback_(reinterpret_cast<const float*>(data), static_cast<size_t>(num_frames), kChannels);
                    }
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
    bool co_initialized_                 = false;
    IAudioClient* audio_client_          = nullptr;
    IAudioCaptureClient* capture_client_ = nullptr;
    std::thread thread_;
    std::vector<float> silence_buf_;
    uint32_t silent_count_       = 0;
    uint32_t first_audio_logged_ = 0;
};

static std::vector<AudioCaptureTarget> enum_endpoints(EDataFlow flow) {
    std::vector<AudioCaptureTarget> results;

    HRESULT hr          = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool co_initialized = SUCCEEDED(hr) || hr == S_FALSE;

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator)
    );
    if (FAILED(hr)) {
        if (co_initialized) CoUninitialize();
        return results;
    }

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    enumerator->Release();
    if (FAILED(hr)) {
        if (co_initialized) CoUninitialize();
        return results;
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device))) continue;

        LPWSTR wid = nullptr;
        if (FAILED(device->GetId(&wid))) {
            device->Release();
            continue;
        }

        int id_len = WideCharToMultiByte(CP_UTF8, 0, wid, -1, nullptr, 0, nullptr, nullptr);
        std::string id(id_len > 0 ? id_len - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wid, -1, id.data(), id_len, nullptr, nullptr);
        CoTaskMemFree(wid);

        std::string name;
        IPropertyStore* props = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT var;
            PropVariantInit(&var);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var)) && var.vt == VT_LPWSTR) {
                int name_len = WideCharToMultiByte(CP_UTF8, 0, var.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                name.resize(name_len > 0 ? name_len - 1 : 0);
                WideCharToMultiByte(CP_UTF8, 0, var.pwszVal, -1, name.data(), name_len, nullptr, nullptr);
            }
            PropVariantClear(&var);
            props->Release();
        }

        results.push_back({std::move(id), std::move(name)});
        device->Release();
    }

    collection->Release();
    if (co_initialized) CoUninitialize();
    return results;
}

std::vector<AudioCaptureTarget> SystemAudioCapture::list_sinks() {
    return enum_endpoints(eRender);
}

std::vector<AudioCaptureTarget> SystemAudioCapture::list_sources() {
    return enum_endpoints(eCapture);
}

bool SystemAudioCapture::available() {
    return true;
}

std::unique_ptr<SystemAudioCapture> SystemAudioCapture::create() {
    return std::make_unique<SystemAudioCaptureWin>();
}

#endif // _WIN32
