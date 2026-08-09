#include "webrtc/google_webrtc_pcm_playout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

bool GoogleWebRtcPcmPlayout::start()
{
    if (device_.running()) {
        return true;
    }
    queue_.reset();
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = static_cast<ma_uint32>(kChannels);
    config.sampleRate = kSampleRate;
    config.dataCallback = &GoogleWebRtcPcmPlayout::data_callback;
    config.pUserData = this;
    accepting_ = true;
    if (device_.start(config)) {
        return true;
    }
    accepting_ = false;
    return false;
}

void GoogleWebRtcPcmPlayout::stop()
{
    accepting_ = false;
    while (active_pushes_.load(std::memory_order_acquire) != 0) {
        std::this_thread::yield();
    }
    device_.stop();
    queue_.reset();
    output_level_ = 0.0f;
}

void GoogleWebRtcPcmPlayout::push(std::span<const int16_t> samples) noexcept
{
    active_pushes_.fetch_add(1, std::memory_order_acq_rel);
    if (!accepting_.load(std::memory_order_acquire) || samples.empty()
        || samples.size() % kChannels != 0
        || queue_.write_available() < samples.size()) {
        active_pushes_.fetch_sub(1, std::memory_order_release);
        return;
    }
    queue_.push(samples.data(), samples.size());
    active_pushes_.fetch_sub(1, std::memory_order_release);
}

void GoogleWebRtcPcmPlayout::set_volume(float volume) noexcept
{
    volume_ = std::clamp(volume, 0.0f, 2.0f);
}

float GoogleWebRtcPcmPlayout::volume() const noexcept
{
    return volume_;
}

void GoogleWebRtcPcmPlayout::set_muted(bool muted) noexcept
{
    muted_ = muted;
}

bool GoogleWebRtcPcmPlayout::muted() const noexcept
{
    return muted_;
}

float GoogleWebRtcPcmPlayout::output_level() const noexcept
{
    return output_level_;
}

void GoogleWebRtcPcmPlayout::data_callback(ma_device* device,
    void* output,
    const void*,
    ma_uint32 frame_count)
{
    auto* self = static_cast<GoogleWebRtcPcmPlayout*>(device->pUserData);
    self->render(static_cast<int16_t*>(output),
        static_cast<size_t>(frame_count) * kChannels);
}

void GoogleWebRtcPcmPlayout::render(
    int16_t* output, size_t samples) noexcept
{
    const size_t read = queue_.pop(output, samples);
    std::fill(output + read, output + samples, int16_t { 0 });

    const float gain = muted_ ? 0.0f : volume_.load();
    int peak = 0;
    for (size_t i = 0; i < read; ++i) {
        const int scaled = static_cast<int>(
            std::lround(static_cast<float>(output[i]) * gain));
        output[i] = static_cast<int16_t>(std::clamp(scaled,
            static_cast<int>(std::numeric_limits<int16_t>::min()),
            static_cast<int>(std::numeric_limits<int16_t>::max())));
        peak = std::max(peak, std::abs(static_cast<int>(output[i])));
    }
    output_level_ = static_cast<float>(peak)
        / static_cast<float>(std::numeric_limits<int16_t>::max());
}
