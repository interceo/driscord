#define MINIAUDIO_IMPLEMENTATION
#include "audio_engine.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "log.hpp"
#include "stream_jitter.hpp"
#include "utils/byte_utils.hpp"

using namespace drist;

AudioEngine::AudioEngine(int voice_jitter_ms)
    : voice_jitter_ms_(voice_jitter_ms),
      capture_buf_(opus::kFrameSize, 0.0f),
      voice_mix_buf_(opus::kFrameSize, 0.0f),
      screen_mix_buf_(opus::kFrameSize, 0.0f),
      encode_buf_(protocol::kAudioHeaderSize + opus::kMaxPacket),
      decode_buf_(opus::kFrameSize) {}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::audio_callback(void* pDevice, void* pOutput, const void* pInput, uint32_t frameCount) {
    auto* dev = static_cast<ma_device*>(pDevice);
    auto* self = static_cast<AudioEngine*>(dev->pUserData);

    if (pInput) {
        self->on_capture(static_cast<const float*>(pInput), frameCount);
    }
    if (pOutput) {
        self->on_playback(static_cast<float*>(pOutput), frameCount);
    }
}

bool AudioEngine::start(PacketCallback on_packet) {
    if (running_) {
        return true;
    }

    on_packet_ = std::move(on_packet);
    capture_pos_ = 0;

    auto enc = std::make_unique<OpusEncode>();
    if (!enc->init(opus::kSampleRate, VOICE_CHANNELS, 64000, 2048 /* OPUS_APPLICATION_VOIP */)) {
        return false;
    }

    auto dec = std::make_unique<OpusDecode>();
    if (!dec->init(opus::kSampleRate, VOICE_CHANNELS)) {
        return false;
    }

    auto dev = std::make_unique<ma_device>();
    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.capture.format = ma_format_f32;
    config.capture.channels = VOICE_CHANNELS;
    config.playback.format = ma_format_f32;
    config.playback.channels = VOICE_CHANNELS;
    config.sampleRate = opus::kSampleRate;
    config.dataCallback = [](ma_device* d, void* out, const void* in, ma_uint32 fc) { audio_callback(d, out, in, fc); };
    config.pUserData = this;
    config.periodSizeInFrames = opus::kFrameSize;

    if (ma_device_init(nullptr, &config, dev.get()) != MA_SUCCESS) {
        LOG_ERROR() << "ma_device_init failed";
        return false;
    }

    if (ma_device_start(dev.get()) != MA_SUCCESS) {
        LOG_ERROR() << "ma_device_start failed";
        ma_device_uninit(dev.get());
        return false;
    }

    encoder_ = std::move(enc);
    decoder_ = std::move(dec);
    device_ = std::move(dev);
    {
        std::scoped_lock lk(voice_mutex_);
        voice_jitters_.clear();
        voice_snapshot_.clear();
    }
    voice_send_seq_ = 0;

    running_ = true;

    LOG_INFO() << "audio engine started";
    return true;
}

void AudioEngine::stop() {
    if (!running_) {
        return;
    }
    running_ = false;

    if (device_) {
        ma_device_stop(device_.get());
        ma_device_uninit(device_.get());
        device_.reset();
    }

    encoder_.reset();
    decoder_.reset();
    {
        std::scoped_lock lk(voice_mutex_);
        voice_jitters_.clear();
        voice_snapshot_.clear();
    }

    LOG_INFO() << "audio engine stopped";
}

void AudioEngine::on_capture(const float* input, uint32_t frames) {
    if (!running_ || !on_packet_) {
        return;
    }

    float sum = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        sum += input[i] * input[i];
    }
    input_level_.store(std::sqrt(sum / static_cast<float>(frames)));

    if (muted_) {
        return;
    }

    uint32_t consumed = 0;
    while (consumed < frames) {
        uint32_t to_copy = std::min(static_cast<uint32_t>(opus::kFrameSize - capture_pos_), frames - consumed);
        std::memcpy(&capture_buf_[capture_pos_], &input[consumed], to_copy * sizeof(float));
        capture_pos_ += to_copy;
        consumed += to_copy;

        if (capture_pos_ == static_cast<size_t>(opus::kFrameSize)) {
            uint8_t* opus_start = encode_buf_.data() + protocol::kAudioHeaderSize;
            int bytes = encoder_->encode(capture_buf_.data(), opus::kFrameSize, opus_start, opus::kMaxPacket);
            if (bytes > 0) {
                write_u16_le(encode_buf_.data(), voice_send_seq_++);
                write_u32_le(encode_buf_.data() + 2, now_ms());
                on_packet_(encode_buf_.data(), protocol::kAudioHeaderSize + static_cast<size_t>(bytes));
            }
            capture_pos_ = 0;
        }
    }
}

void AudioEngine::on_playback(float* output, const uint32_t frames) {
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
        LOG_INFO() << "[audio-engine] playback #" << playback_count_ << " screen_stream_=null";
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

void AudioEngine::feed_packet(const std::string& peer_id, const uint8_t* data, size_t len, float peer_volume) {
    if (!running_ || !decoder_) {
        return;
    }

    if (len <= protocol::kAudioHeaderSize) {
        return;
    }

    uint16_t seq = read_u16_le(data);
    uint32_t sender_ts = read_u32_le(data + 2);
    const uint8_t* opus_data = data + protocol::kAudioHeaderSize;
    int opus_len = static_cast<int>(len - protocol::kAudioHeaderSize);

    const int samples = decoder_->decode(opus_data, opus_len, decode_buf_.data(), opus::kFrameSize);

    if (samples <= 0) {
        return;
    }

    if (peer_volume != 1.0f) {
        for (int i = 0; i < samples * VOICE_CHANNELS; ++i) {
            decode_buf_[static_cast<size_t>(i)] *= peer_volume;
        }
    }

    std::shared_ptr<AudioJitter> jitter;
    {
        std::scoped_lock lk(voice_mutex_);
        auto& slot = voice_jitters_[peer_id];
        if (!slot) {
            slot = std::make_shared<AudioJitter>(static_cast<size_t>(voice_jitter_ms_));
        }
        jitter = slot;
    }
    jitter->push(decode_buf_.data(), static_cast<size_t>(samples), seq, sender_ts);
}

void AudioEngine::remove_voice_peer(const std::string& peer_id) {
    std::scoped_lock lk(voice_mutex_);
    voice_jitters_.erase(peer_id);
}
