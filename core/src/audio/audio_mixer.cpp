#include "audio_mixer.hpp"

#include "audio.hpp"
#include "log.hpp"
#include "opus_codec.hpp"
#include "utils/ma_device.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

AudioMixer::AudioMixer() = default;
AudioMixer::~AudioMixer()
{
    stop();
}

std::string AudioMixer::list_output_devices_json()
{
    ma_context ctx;
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {
        LOG_ERROR()
            << "AudioMixer::list_output_devices_json: ma_context_init failed";
        return "[]";
    }

    ma_device_info* devices = nullptr;
    ma_uint32 count = 0;
    nlohmann::json arr = nlohmann::json::array();

    if (ma_context_get_devices(&ctx, &devices, &count, nullptr, nullptr) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            arr.push_back({ { "id", devices[i].name }, { "name", devices[i].name } });
        }
    } else {
        LOG_ERROR() << "AudioMixer::list_output_devices_json: "
                       "ma_context_get_devices failed";
    }

    ma_context_uninit(&ctx);
    return arr.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// Finds the ma_device_id for a device with the given name from the playback
// list. Returns false if the device was not found.
static bool find_playback_device_id(const std::string& name,
    ma_device_id& out_id)
{
    if (name.empty()) {
        return false;
    }
    ma_context ctx;
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {
        return false;
    }
    ma_device_info* devs = nullptr;
    ma_uint32 count = 0;
    bool found = false;
    if (ma_context_get_devices(&ctx, &devs, &count, nullptr, nullptr) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            if (name == devs[i].name) {
                out_id = devs[i].id;
                found = true;
                break;
            }
        }
    }
    ma_context_uninit(&ctx);
    return found;
}

utils::Expected<void, AudioError> AudioMixer::start()
{
    if (running_) {
        return { };
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = opus::kSampleRate;
    config.dataCallback = [](ma_device* d, void* out, const void* /*in*/,
                              ma_uint32 fc) {
        static_cast<AudioMixer*>(d->pUserData)
            ->on_playback(static_cast<float*>(out), fc);
    };
    config.notificationCallback = [](const ma_device_notification* n) {
        auto* self = static_cast<AudioMixer*>(n->pDevice->pUserData);
        if (n->type == ma_device_notification_type_stopped && self->running_.load()) {
            LOG_WARNING()
                << "AudioMixer: playback device stopped unexpectedly, restarting";
            ma_device_start(n->pDevice);
        } else if (n->type == ma_device_notification_type_rerouted) {
            LOG_INFO() << "AudioMixer: playback device rerouted";
        }
    };
    config.pUserData = this;
    config.periodSizeInFrames = opus::kFrameSize;

    ma_device_id selected_id { };
    if (find_playback_device_id(output_device_id_, selected_id)) {
        config.playback.pDeviceID = &selected_id;
        LOG_INFO() << "AudioMixer: using device '" << output_device_id_ << "'";
    } else if (!output_device_id_.empty()) {
        LOG_WARNING() << "AudioMixer: device '" << output_device_id_
                      << "' not found, using default";
    }

    auto dev = std::make_unique<MaDevice>();
    if (!dev->start(config)) {
        LOG_ERROR() << "AudioMixer: failed to start audio device";
        return utils::Unexpected(AudioError::MixerDeviceStartFailed);
    }

    device_ = std::move(dev);
    running_ = true;
    LOG_INFO() << "AudioMixer: started";
    return { };
}

void AudioMixer::set_output_device(std::string id)
{
    output_device_id_ = std::move(id);
    if (!running_) {
        return;
    }

    // Restart the playback device without clearing sources.
    running_ = false;
    device_.reset();

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = opus::kSampleRate;
    config.dataCallback = [](ma_device* d, void* out, const void* /*in*/,
                              ma_uint32 fc) {
        static_cast<AudioMixer*>(d->pUserData)
            ->on_playback(static_cast<float*>(out), fc);
    };
    config.notificationCallback = [](const ma_device_notification* n) {
        auto* self = static_cast<AudioMixer*>(n->pDevice->pUserData);
        if (n->type == ma_device_notification_type_stopped && self->running_.load()) {
            LOG_WARNING()
                << "AudioMixer: playback device stopped unexpectedly, restarting";
            ma_device_start(n->pDevice);
        } else if (n->type == ma_device_notification_type_rerouted) {
            LOG_INFO() << "AudioMixer: playback device rerouted";
        }
    };
    config.pUserData = this;
    config.periodSizeInFrames = opus::kFrameSize;

    ma_device_id selected_id { };
    if (find_playback_device_id(output_device_id_, selected_id)) {
        config.playback.pDeviceID = &selected_id;
    }

    auto dev = std::make_unique<MaDevice>();
    if (!dev->start(config)) {
        LOG_ERROR() << "AudioMixer: failed to restart with new output device";
        return;
    }
    device_ = std::move(dev);
    running_ = true;
    LOG_INFO() << "AudioMixer: restarted on device '" << output_device_id_ << "'";
}

void AudioMixer::stop()
{
    if (!running_) {
        return;
    }
    running_ = false;
    device_
        .reset(); // MaDevice destructor calls ma_device_stop + ma_device_uninit
    {
        std::scoped_lock lk(sources_mutex_);
        sources_.clear();
        snapshot_.clear();
    }
    LOG_INFO() << "AudioMixer: stopped";
}

void AudioMixer::add_source(std::shared_ptr<AudioReceiver> src)
{
    if (!src) {
        return;
    }
    std::scoped_lock lk(sources_mutex_);
    if (std::find(sources_.begin(), sources_.end(), src) == sources_.end()) {
        sources_.push_back(std::move(src));
    }
}

void AudioMixer::remove_source(const std::shared_ptr<AudioReceiver>& src)
{
    std::scoped_lock lk(sources_mutex_);
    sources_.erase(std::remove(sources_.begin(), sources_.end(), src),
        sources_.end());
}

void AudioMixer::on_playback(float* output, const uint32_t frames)
{
    {
        std::unique_lock lk(sources_mutex_, std::try_to_lock);
        if (lk.owns_lock()) {
            snapshot_ = sources_;
        }
    }

    std::memset(output, 0, frames * sizeof(float));
    if (deafened_) {
        output_level_.store(0.0f);
        return;
    }

    for (const auto& src : snapshot_) {
        if (src->muted()) {
            continue;
        }

        auto samples = src->pop();
        const float vol = src->volume();
        for (size_t i = 0; i < samples.size() && i < frames; ++i) {
            output[i] += samples[i] * vol;
        }
    }

    ++playback_count_;

    const float vol = output_volume_.load();
    float sum = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        output[i] *= vol;
        sum += output[i] * output[i];
    }
    output_level_.store(std::sqrt(sum / frames));
}
