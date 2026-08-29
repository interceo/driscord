#pragma once

#include "utils/ma_device.hpp"

#include <atomic>
#include <boost/lockfree/spsc_queue.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

class GoogleWebRtcPcmPlayout final {
public:
    static constexpr int kSampleRate = 48'000;
    static constexpr size_t kChannels = 2;

    GoogleWebRtcPcmPlayout() = default;
    ~GoogleWebRtcPcmPlayout() = default;

    GoogleWebRtcPcmPlayout(const GoogleWebRtcPcmPlayout&) = delete;
    GoogleWebRtcPcmPlayout& operator=(const GoogleWebRtcPcmPlayout&) = delete;

    bool start();
    void stop();
    void push(std::span<const int16_t> samples) noexcept;

    void set_volume(float volume) noexcept;
    [[nodiscard]] float volume() const noexcept;
    void set_muted(bool muted) noexcept;
    [[nodiscard]] bool muted() const noexcept;
    [[nodiscard]] float output_level() const noexcept;

private:
    static void data_callback(ma_device* device,
        void* output,
        const void* input,
        ma_uint32 frame_count);
    void render(int16_t* output, size_t samples) noexcept;

    boost::lockfree::spsc_queue<int16_t,
        boost::lockfree::capacity<kSampleRate * kChannels * 2>>
        queue_;
    MaDevice device_;
    std::atomic<bool> accepting_ { false };
    std::atomic<unsigned> active_pushes_ { 0 };
    std::atomic<float> volume_ { 1.0f };
    std::atomic<float> output_level_ { 0.0f };
    std::atomic<bool> muted_ { false };
};
