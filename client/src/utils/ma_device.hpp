#pragma once

#include <miniaudio.h>

class MaDevice {
public:
    MaDevice() = default;

    ~MaDevice() { stop(); }

    MaDevice(const MaDevice&) = delete;
    MaDevice& operator=(const MaDevice&) = delete;

    bool start(const ma_device_config& cfg);

    void stop();
    bool running() const noexcept { return running_; }

    ma_device* get() noexcept { return running_ ? &dev_ : nullptr; }

private:
    ma_device dev_{};
    bool running_ = false;
};
