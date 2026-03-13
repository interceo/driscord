#define MINIAUDIO_IMPLEMENTATION
#include "audio_engine.hpp"

#include <miniaudio.h>
#include <opus.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

AudioEngine::AudioEngine() : capture_buf_(FRAME_SIZE, 0.0f), encode_buf_(MAX_OPUS_PACKET), decode_buf_(FRAME_SIZE) {}

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

    int err;
    encoder_ = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        std::cerr << "opus_encoder_create failed: " << opus_strerror(err) << std::endl;
        return false;
    }

    opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(64000));
    opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    decoder_ = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    if (err != OPUS_OK) {
        std::cerr << "opus_decoder_create failed: " << opus_strerror(err) << std::endl;
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
        return false;
    }

    device_ = std::make_unique<ma_device>();
    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.capture.format = ma_format_f32;
    config.capture.channels = CHANNELS;
    config.playback.format = ma_format_f32;
    config.playback.channels = CHANNELS;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = [](ma_device* d, void* out, const void* in, ma_uint32 fc) { audio_callback(d, out, in, fc); };
    config.pUserData = this;
    config.periodSizeInFrames = FRAME_SIZE;

    if (ma_device_init(nullptr, &config, device_.get()) != MA_SUCCESS) {
        std::cerr << "ma_device_init failed" << std::endl;
        goto err;
    }

    if (ma_device_start(device_.get()) != MA_SUCCESS) {
        std::cerr << "ma_device_start failed" << std::endl;
        ma_device_uninit(device_.get());
        goto err;
    }

    playback_ring_.clear();
    running_ = true;
    std::cout << "audio engine started" << std::endl;
    return true;

err:
    opus_encoder_destroy(encoder_);
    encoder_ = nullptr;
    opus_decoder_destroy(decoder_);
    decoder_ = nullptr;
    device_.reset();
    return false;
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

    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }

    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }

    std::cout << "audio engine stopped" << std::endl;
}

void AudioEngine::on_capture(const float* input, uint32_t frames) {
    if (!running_ || !on_packet_) {
        return;
    }

    // Compute input level (RMS)
    float sum = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        sum += input[i] * input[i];
    }

    float rms = std::sqrt(sum / static_cast<float>(frames));
    input_level_.store(rms);

    if (muted_) {
        return;
    }

    uint32_t consumed = 0;
    while (consumed < frames) {
        uint32_t to_copy = std::min(static_cast<uint32_t>(FRAME_SIZE - capture_pos_), frames - consumed);
        std::memcpy(&capture_buf_[capture_pos_], &input[consumed], to_copy * sizeof(float));

        capture_pos_ += to_copy;
        consumed += to_copy;

        if (capture_pos_ == static_cast<size_t>(FRAME_SIZE)) {
            int bytes =
                opus_encode_float(encoder_, capture_buf_.data(), FRAME_SIZE, encode_buf_.data(), MAX_OPUS_PACKET);

            if (bytes > 0) {
                on_packet_(encode_buf_.data(), static_cast<size_t>(bytes));
            }
            capture_pos_ = 0;
        }
    }
}

void AudioEngine::on_playback(float* output, uint32_t frames) {
    size_t got = playback_ring_.read(output, frames);

    // Fill remainder with silence
    if (got < frames) {
        std::memset(&output[got], 0, (frames - got) * sizeof(float));
    }

    float vol = output_volume_.load();
    float sum = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        output[i] *= vol;
        sum += output[i] * output[i];
    }
    output_level_.store(std::sqrt(sum / static_cast<float>(frames)));
}

void AudioEngine::feed_packet(const uint8_t* data, size_t len) {
    if (!running_ || !decoder_) {
        return;
    }

    int samples = opus_decode_float(decoder_, data, static_cast<int>(len), decode_buf_.data(), FRAME_SIZE, 0);

    if (samples > 0) {
        playback_ring_.write(decode_buf_.data(), static_cast<size_t>(samples));
    }
}
