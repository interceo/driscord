#include "audio_mixer.hpp"

#include "audio.hpp"
#include "log.hpp"
#include "utils/ma_device.hpp"
#include "utils/opus_codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

AudioMixer::AudioMixer() = default;
AudioMixer::~AudioMixer() {
    stop();
}

bool AudioMixer::start() {
    if (running_) {
        return true;
    }

    ma_device_config config  = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate        = opus::kSampleRate;

    config.dataCallback = [](ma_device* d, void* out, const void* /*in*/, ma_uint32 fc) {
        static_cast<AudioMixer*>(d->pUserData)->on_playback(static_cast<float*>(out), fc);
    };
    config.notificationCallback = [](const ma_device_notification* n) {
        auto* self = static_cast<AudioMixer*>(n->pDevice->pUserData);
        if (n->type == ma_device_notification_type_stopped && self->running_.load()) {
            LOG_WARNING() << "AudioMixer: playback device stopped unexpectedly, restarting";
            ma_device_start(n->pDevice);
        } else if (n->type == ma_device_notification_type_rerouted) {
            LOG_INFO() << "AudioMixer: playback device rerouted";
        }
    };

    config.pUserData          = this;
    config.periodSizeInFrames = opus::kFrameSize;

    auto dev = std::make_unique<MaDevice>();
    if (!dev->start(config)) {
        LOG_ERROR() << "AudioMixer: failed to start audio device";
        return false;
    }

    device_  = std::move(dev);
    running_ = true;
    LOG_INFO() << "AudioMixer: started";
    return true;
}

void AudioMixer::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    device_.reset(); // MaDevice destructor calls ma_device_stop + ma_device_uninit
    {
        std::scoped_lock lk(sources_mutex_);
        sources_.clear();
        snapshot_.clear();
    }
    LOG_INFO() << "AudioMixer: stopped";
}

void AudioMixer::add_source(std::shared_ptr<AudioReceiver> src) {
    if (!src) {
        return;
    }
    std::scoped_lock lk(sources_mutex_);
    if (std::find(sources_.begin(), sources_.end(), src) == sources_.end()) {
        sources_.push_back(std::move(src));
    }
}

void AudioMixer::remove_source(const std::shared_ptr<AudioReceiver>& src) {
    std::scoped_lock lk(sources_mutex_);
    sources_.erase(std::remove(sources_.begin(), sources_.end(), src), sources_.end());
}

void AudioMixer::on_playback(float* output, const uint32_t frames) {
    {
        std::unique_lock lk(sources_mutex_, std::try_to_lock);
        if (lk.owns_lock()) {
            snapshot_ = sources_;
        }
    }

    std::memset(output, 0, frames * sizeof(float));

    for (const auto& src : snapshot_) {
        if (src->muted()) continue;
        auto samples = src->pop();
        const float vol = src->volume();
        for (size_t i = 0; i < samples.size() && i < frames; ++i) {
            output[i] += samples[i] * vol;
        }
    }

    ++playback_count_;

    if (deafened_) {
        std::memset(output, 0, frames * sizeof(float));
        output_level_.store(0.0f);
        return;
    }

    const float vol = output_volume_.load();
    float sum       = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        output[i] *= vol;
        sum += output[i] * output[i];
    }
    output_level_.store(std::sqrt(sum / static_cast<float>(frames)));
}
