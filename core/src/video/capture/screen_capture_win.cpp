#include "screen_capture.hpp"
#include "screen_capture_common.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "log.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

// --- helpers ----------------------------------------------------------------

static std::string wstr_to_utf8(const wchar_t* wstr)
{
    if (!wstr || !*wstr) {
        return { };
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return { };
    }
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr,
        nullptr);
    return result;
}

// --- target enumeration -----------------------------------------------------

struct MonitorEnumData {
    std::vector<ScreenCaptureTarget>* targets;
    int index;
};

static BOOL CALLBACK monitor_enum_proc(HMONITOR hmon,
    HDC,
    LPRECT,
    LPARAM lparam)
{
    auto* data = reinterpret_cast<MonitorEnumData*>(lparam);

    MONITORINFOEXW mi { };
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi)) {
        return TRUE;
    }

    int w = mi.rcMonitor.right - mi.rcMonitor.left;
    int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

    ScreenCaptureTarget t;
    t.type = ScreenCaptureTarget::Monitor;
    t.id = std::to_string(data->index);
    t.name = "Monitor " + std::to_string(data->index + 1) + " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
    t.width = w;
    t.height = h;
    t.x = mi.rcMonitor.left;
    t.y = mi.rcMonitor.top;

    data->targets->emplace_back(std::move(t));
    data->index++;
    return TRUE;
}

static BOOL CALLBACK window_enum_proc(HWND hwnd, LPARAM lparam)
{
    auto* targets = reinterpret_cast<std::vector<ScreenCaptureTarget>*>(lparam);

    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    wchar_t title[256] { };
    if (GetWindowTextW(hwnd, title, 256) <= 0) {
        return TRUE;
    }

    RECT rect { };
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w < 50 || h < 50) {
        return TRUE;
    }

    DWORD ex_style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    if (ex_style & WS_EX_TOOLWINDOW) {
        return TRUE;
    }

    ScreenCaptureTarget t;
    t.type = ScreenCaptureTarget::Window;
    t.id = std::to_string(reinterpret_cast<uintptr_t>(hwnd));
    t.name = wstr_to_utf8(title) + " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
    t.width = w;
    t.height = h;

    targets->push_back(std::move(t));
    return TRUE;
}

std::vector<ScreenCaptureTarget> ScreenCapture::list_targets()
{
    std::vector<ScreenCaptureTarget> targets;

    MonitorEnumData mon_data { &targets, 0 };
    EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc,
        reinterpret_cast<LPARAM>(&mon_data));

    EnumWindows(window_enum_proc, reinterpret_cast<LPARAM>(&targets));

    return targets;
}

// --- thumbnail (BitBlt) -----------------------------------------------------

ScreenCapture::Frame ScreenCapture::grab_thumbnail(
    const ScreenCaptureTarget& target,
    int max_w,
    int max_h)
{
    Frame f;

    HDC src_dc = nullptr;
    int src_x = 0, src_y = 0, src_w = 0, src_h = 0;
    HWND hwnd = nullptr;

    if (target.type == ScreenCaptureTarget::Window && !target.id.empty()) {
        try {
            hwnd = reinterpret_cast<HWND>(
                static_cast<uintptr_t>(std::stoull(target.id)));
        } catch (const std::exception&) {
            return f;
        }
        if (!IsWindow(hwnd)) {
            return f;
        }

        RECT rect { };
        GetClientRect(hwnd, &rect);
        src_w = rect.right - rect.left;
        src_h = rect.bottom - rect.top;
        src_dc = GetDC(hwnd);
    } else {
        src_dc = GetDC(nullptr);
        src_x = target.x;
        src_y = target.y;
        src_w = target.width;
        src_h = target.height;
    }

    if (!src_dc || src_w <= 0 || src_h <= 0) {
        if (src_dc) {
            ReleaseDC(hwnd, src_dc);
        }
        return f;
    }

    HDC mem_dc = CreateCompatibleDC(src_dc);
    HBITMAP bmp = CreateCompatibleBitmap(src_dc, src_w, src_h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, bmp);

    BitBlt(mem_dc, 0, 0, src_w, src_h, src_dc, src_x, src_y, SRCCOPY);

    BITMAPINFOHEADER bi { };
    bi.biSize = sizeof(bi);
    bi.biWidth = src_w;
    bi.biHeight = -src_h; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    std::vector<uint8_t> bgra(static_cast<size_t>(src_w) * src_h * 4);
    GetDIBits(mem_dc, bmp, 0, static_cast<UINT>(src_h), bgra.data(),
        reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem_dc);
    ReleaseDC(hwnd, src_dc);

    int ow, oh;
    compute_output_size(src_w, src_h, max_w, max_h, ow, oh);

    f.width = ow;
    f.height = oh;
    f.data.resize(static_cast<size_t>(ow) * oh * 4);

    if (ow == src_w && oh == src_h) {
        f.data = std::move(bgra);
    } else {
        scale_nearest(bgra.data(), src_w, src_h, f.data.data(), ow, oh);
    }
    return f;
}

// --- DXGI Desktop Duplication + BitBlt capture ------------------------------

class WinScreenCapture : public ScreenCapture {
public:
    ~WinScreenCapture() override { stop(); }

    bool start(int fps,
        const ScreenCaptureTarget& target,
        int max_w,
        int max_h,
        FrameCallback cb) override
    {
        if (running_) {
            return false;
        }

        callback_ = std::move(cb);
        max_w_ = max_w;
        max_h_ = max_h;
        frame_interval_us_ = 1000000 / std::max(fps, 1);
        target_ = target;

        if (target.type == ScreenCaptureTarget::Window) {
            try {
                hwnd_ = reinterpret_cast<HWND>(
                    static_cast<uintptr_t>(std::stoull(target.id)));
            } catch (const std::exception&) {
                LOG_ERROR() << "invalid window id: " << target.id;
                return false;
            }
            if (!IsWindow(hwnd_)) {
                LOG_ERROR() << "invalid window handle";
                return false;
            }
            running_ = true;
            thread_ = std::thread(&WinScreenCapture::bitblt_capture_loop, this);
            LOG_INFO() << "screen capture started (BitBlt window) @ " << fps
                       << " fps";
        } else {
            if (!init_dxgi()) {
                LOG_ERROR() << "failed to init DXGI desktop duplication";
                return false;
            }
            running_ = true;
            thread_ = std::thread(&WinScreenCapture::dxgi_capture_loop, this);
            LOG_INFO() << "screen capture started (DXGI) " << capture_w_ << "x"
                       << capture_h_ << " @ " << fps << " fps";
        }
        return true;
    }

    void stop() override
    {
        if (!running_.exchange(false)) {
            return;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        cleanup_dxgi();
        LOG_INFO() << "screen capture stopped";
    }

    bool running() const override { return running_; }

private:
    // --- DXGI init / cleanup ------------------------------------------------

    bool init_dxgi()
    {
        D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL feature_level { };

        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, feature_levels, 1, D3D11_SDK_VERSION,
            &device_, &feature_level, &context_);
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                feature_levels, 1, D3D11_SDK_VERSION, &device_,
                &feature_level, &context_);
        }
        if (FAILED(hr)) {
            LOG_ERROR() << "D3D11CreateDevice failed: 0x" << std::hex << hr;
            return false;
        }

        IDXGIDevice* dxgi_device = nullptr;
        hr = device_->QueryInterface(__uuidof(IDXGIDevice),
            reinterpret_cast<void**>(&dxgi_device));
        if (FAILED(hr)) {
            LOG_ERROR() << "QueryInterface(IDXGIDevice) failed";
            return false;
        }

        IDXGIAdapter* adapter = nullptr;
        hr = dxgi_device->GetAdapter(&adapter);
        dxgi_device->Release();
        if (FAILED(hr)) {
            LOG_ERROR() << "GetAdapter failed";
            return false;
        }

        // Find the DXGI output that matches target coordinates
        IDXGIOutput* output = find_output(adapter);
        adapter->Release();
        if (!output) {
            LOG_ERROR() << "no matching DXGI output found";
            return false;
        }

        DXGI_OUTPUT_DESC output_desc { };
        output->GetDesc(&output_desc);
        capture_w_ = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
        capture_h_ = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;

        IDXGIOutput1* output1 = nullptr;
        hr = output->QueryInterface(__uuidof(IDXGIOutput1),
            reinterpret_cast<void**>(&output1));
        output->Release();
        if (FAILED(hr)) {
            LOG_ERROR() << "QueryInterface(IDXGIOutput1) failed";
            return false;
        }

        hr = output1->DuplicateOutput(device_, &duplication_);
        output1->Release();
        if (FAILED(hr)) {
            LOG_ERROR() << "DuplicateOutput failed: 0x" << std::hex << hr;
            return false;
        }

        D3D11_TEXTURE2D_DESC staging_desc { };
        staging_desc.Width = static_cast<UINT>(capture_w_);
        staging_desc.Height = static_cast<UINT>(capture_h_);
        staging_desc.MipLevels = 1;
        staging_desc.ArraySize = 1;
        staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        staging_desc.SampleDesc.Count = 1;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = device_->CreateTexture2D(&staging_desc, nullptr, &staging_tex_);
        if (FAILED(hr)) {
            LOG_ERROR() << "CreateTexture2D(staging) failed";
            return false;
        }

        LOG_INFO() << "DXGI capture init: " << capture_w_ << "x" << capture_h_;
        return true;
    }

    IDXGIOutput* find_output(IDXGIAdapter* adapter)
    {
        IDXGIOutput* best = nullptr;

        for (UINT i = 0;; ++i) {
            IDXGIOutput* out = nullptr;
            if (adapter->EnumOutputs(i, &out) == DXGI_ERROR_NOT_FOUND) {
                break;
            }

            DXGI_OUTPUT_DESC desc { };
            out->GetDesc(&desc);

            int ox = desc.DesktopCoordinates.left;
            int oy = desc.DesktopCoordinates.top;

            if (ox == target_.x && oy == target_.y) {
                if (best) {
                    best->Release();
                }
                return out;
            }

            if (!best) {
                best = out;
            } else {
                out->Release();
            }
        }
        return best; // fallback to first output
    }

    void cleanup_dxgi()
    {
        if (staging_tex_) {
            staging_tex_->Release();
            staging_tex_ = nullptr;
        }
        if (duplication_) {
            duplication_->Release();
            duplication_ = nullptr;
        }
        if (context_) {
            context_->Release();
            context_ = nullptr;
        }
        if (device_) {
            device_->Release();
            device_ = nullptr;
        }
        capture_w_ = 0;
        capture_h_ = 0;
    }

    // --- capture loops ------------------------------------------------------

    void dxgi_capture_loop()
    {
        while (running_) {
            auto t0 = std::chrono::steady_clock::now();

            IDXGIResource* resource = nullptr;
            DXGI_OUTDUPL_FRAME_INFO frame_info { };

            HRESULT hr = duplication_->AcquireNextFrame(100, &frame_info, &resource);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                continue;
            }
            if (FAILED(hr)) {
                if (hr == DXGI_ERROR_ACCESS_LOST) {
                    LOG_WARNING() << "DXGI access lost, reinitializing...";
                    cleanup_dxgi();
                    if (!init_dxgi()) {
                        LOG_ERROR() << "DXGI reinit failed, stopping capture";
                        running_ = false;
                    }
                }
                continue;
            }

            ID3D11Texture2D* frame_tex = nullptr;
            hr = resource->QueryInterface(__uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&frame_tex));
            resource->Release();

            if (SUCCEEDED(hr) && running_) {
                context_->CopyResource(staging_tex_, frame_tex);
                frame_tex->Release();

                D3D11_MAPPED_SUBRESOURCE mapped { };
                hr = context_->Map(staging_tex_, 0, D3D11_MAP_READ, 0, &mapped);
                if (SUCCEEDED(hr)) {
                    deliver_dxgi_frame(static_cast<const uint8_t*>(mapped.pData),
                        static_cast<int>(mapped.RowPitch), capture_w_,
                        capture_h_);
                    context_->Unmap(staging_tex_, 0);
                }
            } else if (frame_tex) {
                frame_tex->Release();
            }

            duplication_->ReleaseFrame();

            auto elapsed = std::chrono::steady_clock::now() - t0;
            auto target_dur = std::chrono::microseconds(frame_interval_us_);
            if (elapsed < target_dur) {
                std::this_thread::sleep_for(target_dur - elapsed);
            }
        }
    }

    void bitblt_capture_loop()
    {
        while (running_) {
            auto t0 = std::chrono::steady_clock::now();

            if (!IsWindow(hwnd_)) {
                LOG_WARNING() << "captured window closed";
                running_ = false;
                break;
            }

            RECT rect { };
            GetClientRect(hwnd_, &rect);
            int w = rect.right - rect.left;
            int h = rect.bottom - rect.top;

            if (w > 0 && h > 0) {
                HDC wnd_dc = GetDC(hwnd_);
                if (wnd_dc) {
                    HDC mem_dc = CreateCompatibleDC(wnd_dc);
                    HBITMAP bmp = CreateCompatibleBitmap(wnd_dc, w, h);
                    HGDIOBJ old = SelectObject(mem_dc, bmp);

                    BitBlt(mem_dc, 0, 0, w, h, wnd_dc, 0, 0, SRCCOPY);

                    BITMAPINFOHEADER bi { };
                    bi.biSize = sizeof(bi);
                    bi.biWidth = w;
                    bi.biHeight = -h;
                    bi.biPlanes = 1;
                    bi.biBitCount = 32;
                    bi.biCompression = BI_RGB;

                    std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
                    GetDIBits(mem_dc, bmp, 0, static_cast<UINT>(h), bgra.data(),
                        reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

                    SelectObject(mem_dc, old);
                    DeleteObject(bmp);
                    DeleteDC(mem_dc);
                    ReleaseDC(hwnd_, wnd_dc);

                    int ow, oh;
                    compute_output_size(w, h, max_w_, max_h_, ow, oh);

                    Frame out;
                    out.width = ow;
                    out.height = oh;

                    if (ow == w && oh == h) {
                        out.data = std::move(bgra);
                    } else {
                        out.data.resize(static_cast<size_t>(ow) * oh * 4);
                        scale_nearest(bgra.data(), w, h, out.data.data(), ow, oh);
                    }

                    if (callback_ && running_) {
                        callback_(std::move(out));
                    }
                }
            }

            auto elapsed = std::chrono::steady_clock::now() - t0;
            auto target_dur = std::chrono::microseconds(frame_interval_us_);
            if (elapsed < target_dur) {
                std::this_thread::sleep_for(target_dur - elapsed);
            }
        }
    }

    void deliver_dxgi_frame(const uint8_t* data, int row_pitch, int w, int h)
    {
        int ow, oh;
        compute_output_size(w, h, max_w_, max_h_, ow, oh);

        int src_stride = w * 4;

        Frame out;
        out.width = ow;
        out.height = oh;

        if (ow == w && oh == h) {
            out.data.resize(static_cast<size_t>(w) * h * 4);
            if (row_pitch == src_stride) {
                std::memcpy(out.data.data(), data, out.data.size());
            } else {
                for (int y = 0; y < h; ++y) {
                    std::memcpy(out.data.data() + y * src_stride, data + y * row_pitch,
                        static_cast<size_t>(src_stride));
                }
            }
        } else {
            std::vector<uint8_t> full(static_cast<size_t>(w) * h * 4);
            if (row_pitch == src_stride) {
                std::memcpy(full.data(), data, full.size());
            } else {
                for (int y = 0; y < h; ++y) {
                    std::memcpy(full.data() + y * src_stride, data + y * row_pitch,
                        static_cast<size_t>(src_stride));
                }
            }
            out.data.resize(static_cast<size_t>(ow) * oh * 4);
            scale_nearest(full.data(), w, h, out.data.data(), ow, oh);
        }

        if (callback_ && running_) {
            callback_(std::move(out));
        }
    }

    // --- state --------------------------------------------------------------

    std::atomic<bool> running_ { false };
    FrameCallback callback_;
    std::thread thread_;
    ScreenCaptureTarget target_;
    int max_w_ = 1920;
    int max_h_ = 1080;
    int frame_interval_us_ = 16666;

    // DXGI (monitor capture)
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGIOutputDuplication* duplication_ = nullptr;
    ID3D11Texture2D* staging_tex_ = nullptr;
    int capture_w_ = 0;
    int capture_h_ = 0;

    // BitBlt (window capture)
    HWND hwnd_ = nullptr;
};

std::unique_ptr<ScreenCapture> ScreenCapture::create()
{
    return std::make_unique<WinScreenCapture>();
}
