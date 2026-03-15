#include "audio_receiver.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "log.hpp"
#include "utils/opus_codec.hpp"
#include "video/stream_jitter.hpp"  // ScreenJitter

using namespace utils;

AudioReceiver::AudioReceiver() = default;
AudioReceiver::AudioReceiver(int jitter_ms) : jitter_ms_(jitter_ms) {}
AudioReceiver::~AudioReceiver() { stop(); }

bool AudioReceiver::start() {
    if (running_) {
        return true;
    }

    auto dec = std::make_unique<OpusDecode>();
    if (!dec->init(opus::kSampleRate, 1 /* mono */)) {
        LOG_ERROR() << "AudioReceiver: failed to init Opus decoder";
        return false;
    }

    auto dev = std::make_unique<ma_device>();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = opus::kSampleRate;
    config.dataCallback = [](ma_device* d, void* out, const void* /*in*/, ma_uint32 fc) {
        static_cast<AudioReceiver*>(d->pUserData)->on_playback(static_cast<float*>(out), fc);
    };
    config.pUserData = this;
    config.periodSizeInFrames = opus::kFrameSize;

    if (ma_device_init(nullptr, &config, dev.get()) != MA_SUCCESS) {
        LOG_ERROR() << "AudioReceiver: ma_device_init failed";
        return false;
    }
    if (ma_device_start(dev.get()) != MA_SUCCESS) {
        LOG_ERROR() << "AudioReceiver: ma_device_start failed";
        ma_device_uninit(dev.get());
        return false;
    }

    decode_buf_.resize(opus::kFrameSize);
    voice_mix_buf_.resize(opus::kFrameSize);
    screen_mix_buf_.resize(opus::kFrameSize);
    {
        std::scoped_lock lk(voice_mutex_);
        voice_jitters_.clear();
        voice_snapshot_.clear();
    }
    playback_count_ = 0;
    decoder_ = std::move(dec);
    device_ = std::move(dev);
    running_ = true;

    LOG_INFO() << "AudioReceiver: started";
    return true;
}

void AudioReceiver::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (device_) {
        ma_device_stop(device_.get());
        ma_device_uninit(device_.get());
        device_.reset();
    }
    decoder_.reset();
    {
        std::scoped_lock lk(voice_mutex_);
        voice_jitters_.clear();
        voice_snapshot_.clear();
    }
    LOG_INFO() << "AudioReceiver: stopped";
}

void AudioReceiver::feed_packet(const std::string& peer_id, const uint8_t* data, size_t len, float peer_volume) {
    if (!running_ || !decoder_) {
        return;
    }
    if (len <= protocol::AudioHeader::kWireSize) {
        return;
    }

    const auto ah = protocol::AudioHeader::deserialize(data);
    const uint8_t* opus_data = data + protocol::AudioHeader::kWireSize;
    int opus_len = static_cast<int>(len - protocol::AudioHeader::kWireSize);

    const int samples = decoder_->decode(opus_data, opus_len, decode_buf_.data(), opus::kFrameSize);
    if (samples <= 0) {
        return;
    }

    if (peer_volume != 1.0f) {
        for (int i = 0; i < samples; ++i) {
            decode_buf_[static_cast<size_t>(i)] *= peer_volume;
        }
    }

    std::shared_ptr<AudioJitter> jitter;
    {
        std::scoped_lock lk(voice_mutex_);
        auto& slot = voice_jitters_[peer_id];
        if (!slot) {
            slot = std::make_shared<AudioJitter>(static_cast<size_t>(jitter_ms_));
        }
        jitter = slot;
    }
    jitter->push(decode_buf_.data(), static_cast<size_t>(samples), ah.seq, ah.sender_ts);
}

void AudioReceiver::remove_voice_peer(const std::string& peer_id) {
    std::scoped_lock lk(voice_mutex_);
    voice_jitters_.erase(peer_id);
}

void AudioReceiver::on_playback(float* output, const uint32_t frames) {
    {
        std::unique_lock lk(voice_mutex_, std::try_to_lock);
        if (lk.owns_lock()) {
            voice_snapshot_.clear();
            voice_snapshot_.reserve(voice_jitters_.size());
            for (auto& [id, j] : voice_jitters_) {
                voice_snapshot_.push_back(j);
            }
        }
    }

    std::memset(output, 0, frames * sizeof(float));

    if (voice_mix_buf_.size() < frames) {
        voice_mix_buf_.resize(frames);
    }
    for (auto& j : voice_snapshot_) {
        j->pop(voice_mix_buf_.data(), frames);
        for (uint32_t i = 0; i < frames; ++i) {
            output[i] += voice_mix_buf_[i];
        }
    }

    ++playback_count_;
    if (screen_stream_) {
        if (screen_mix_buf_.size() < frames) {
            screen_mix_buf_.resize(frames);
        }
        screen_stream_->pop_audio(screen_mix_buf_.data(), frames);
        if (!sharing_screen_audio_) {
            for (uint32_t i = 0; i < frames; ++i) {
                output[i] += screen_mix_buf_[i];
            }
        }
    } else if (playback_count_ % 500 == 0) {
        LOG_INFO() << "[audio-receiver] playback #" << playback_count_ << " screen_stream_=null";
    }

    if (deafened_) {
        std::memset(output, 0, frames * sizeof(float));
        output_level_.store(0.0f);
        return;
    }

    const float vol = output_volume_.load();
    float sum = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        output[i] *= vol;
        sum += output[i] * output[i];
    }
    output_level_.store(std::sqrt(sum / static_cast<float>(frames)));
}
