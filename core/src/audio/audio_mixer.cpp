#include "audio_mixer.hpp"

#include "audio.hpp"
#include "log.hpp"
#include "opus_codec.hpp"
#include "utils/ma_device.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    config.playback.channels = kOutputChannels;
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
    LOG_INFO() << "AudioMixer: started (stereo)";
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
    config.playback.channels = kOutputChannels;
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

// Redistribute pan positions evenly across the stereo field.
static void distribute_pan(std::vector<std::shared_ptr<AudioReceiver>>& sources)
{
    const size_t n = sources.size();
    if (n == 0) {
        return;
    }
    if (n == 1) {
        sources[0]->set_pan(0.5f);
        return;
    }
    // Spread from 0.2 to 0.8 (avoid hard panning).
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        sources[i]->set_pan(0.2f + t * 0.6f);
    }
}

void AudioMixer::add_source(std::shared_ptr<AudioReceiver> src)
{
    if (!src) {
        return;
    }
    std::scoped_lock lk(sources_mutex_);
    if (std::find(sources_.begin(), sources_.end(), src) == sources_.end()) {
        sources_.push_back(std::move(src));
        distribute_pan(sources_);
    }
}

void AudioMixer::remove_source(const std::shared_ptr<AudioReceiver>& src)
{
    std::scoped_lock lk(sources_mutex_);
    sources_.erase(std::remove(sources_.begin(), sources_.end(), src),
        sources_.end());
    distribute_pan(sources_);
}

void AudioMixer::on_playback(float* output, const uint32_t frames)
{
    {
        std::unique_lock lk(sources_mutex_, std::try_to_lock);
        if (lk.owns_lock()) {
            snapshot_ = sources_;
        }
    }

    // Stereo interleaved: output[2*i] = L, output[2*i+1] = R.
    const size_t total_samples = static_cast<size_t>(frames) * kOutputChannels;
    std::memset(output, 0, total_samples * sizeof(float));
    if (deafened_) {
        output_level_.store(0.0f);
        return;
    }

    for (const auto& src : snapshot_) {
        if (src->muted()) {
            continue;
        }

        auto samples = src->pop();
        if (samples.empty()) {
            continue;
        }

        const float vol = src->volume();
        const float pan = src->pan(); // 0=left, 0.5=center, 1=right

        // Constant-power panning.
        const float angle = pan * static_cast<float>(M_PI * 0.5);
        const float gain_l = std::cos(angle) * vol;
        const float gain_r = std::sin(angle) * vol;

        const size_t n = std::min(samples.size(), static_cast<size_t>(frames));
        for (size_t i = 0; i < n; ++i) {
            output[2 * i] += samples[i] * gain_l;
            output[2 * i + 1] += samples[i] * gain_r;
        }
    }

    ++playback_count_;

    const float vol = output_volume_.load();
    float sum = 0.0f;
    for (size_t i = 0; i < total_samples; ++i) {
        float x = output[i] * vol;
        // Soft-clip: linear below 0.8, smooth tanh curve above.
        const float ax = std::abs(x);
        if (ax > 0.8f) {
            const float sign = x > 0.0f ? 1.0f : -1.0f;
            x = sign * (0.8f + 0.2f * std::tanh((ax - 0.8f) * 5.0f));
        }
        output[i] = x;
        sum += x * x;
    }
    output_level_.store(std::sqrt(sum / static_cast<float>(total_samples)));
}
