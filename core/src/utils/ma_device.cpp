#include "ma_device.hpp"

bool MaDevice::start(const ma_device_config& cfg) {
    if (running_) {
        return true;
    }
    if (ma_device_init(nullptr, &cfg, &dev_) != MA_SUCCESS) {
        return false;
    }
    if (ma_device_start(&dev_) != MA_SUCCESS) {
        ma_device_uninit(&dev_);
        return false;
    }
    running_ = true;
    return true;
}

void MaDevice::stop() {
    if (!running_) {
        return;
    }
    ma_device_stop(&dev_);
    ma_device_uninit(&dev_);
    running_ = false;
}