#define MINIAUDIO_IMPLEMENTATION
#include "audio_engine.hpp"

#include <miniaudio.h>
#include <opus.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "log.hpp"

void OpusEncoderDeleter::operator()(OpusEncoder* e) const { opus_encoder_destroy(e); }
void OpusDecoderDeleter::operator()(OpusDecoder* d) const { opus_decoder_destroy(d); }

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
    OpusEncoderPtr enc(opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err));
    if (err != OPUS_OK) {
        LOG_ERROR() << "opus_encoder_create failed: " << opus_strerror(err);
        return false;
    }
    opus_encoder_ctl(enc.get(), OPUS_SET_BITRATE(64000));
    opus_encoder_ctl(enc.get(), OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    OpusDecoderPtr dec(opus_decoder_create(SAMPLE_RATE, CHANNELS, &err));
    if (err != OPUS_OK) {
        LOG_ERROR() << "opus_decoder_create failed: " << opus_strerror(err);
        return false;
    }

    auto dev = std::make_unique<ma_device>();
    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.capture.format = ma_format_f32;
    config.capture.channels = CHANNELS;
    config.playback.format = ma_format_f32;
    config.playback.channels = CHANNELS;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = [](ma_device* d, void* out, const void* in, ma_uint32 fc) { audio_callback(d, out, in, fc); };
    config.pUserData = this;
    config.periodSizeInFrames = FRAME_SIZE;

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
    playback_ring_.clear();
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
        uint32_t to_copy = std::min(static_cast<uint32_t>(FRAME_SIZE - capture_pos_), frames - consumed);
        std::memcpy(&capture_buf_[capture_pos_], &input[consumed], to_copy * sizeof(float));
        capture_pos_ += to_copy;
        consumed += to_copy;

        if (capture_pos_ == static_cast<size_t>(FRAME_SIZE)) {
            int bytes =
                opus_encode_float(encoder_.get(), capture_buf_.data(), FRAME_SIZE, encode_buf_.data(), MAX_OPUS_PACKET);
            if (bytes > 0) {
                on_packet_(encode_buf_.data(), static_cast<size_t>(bytes));
            }
            capture_pos_ = 0;
        }
    }
}

void AudioEngine::on_playback(float* output, const uint32_t frames) {
    if (deafened_) {
        std::memset(output, 0, frames * sizeof(float));
        playback_ring_.read(output, frames);
        std::memset(output, 0, frames * sizeof(float));
        output_level_.store(0.0f);
        return;
    }

    const size_t got = playback_ring_.read(output, frames);
    if (got < frames) {
        std::memset(&output[got], 0, (frames - got) * sizeof(float));
    }

    const float vol = output_volume_.load();
    float sum = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        output[i] *= vol;
        sum += output[i] * output[i];
    }

    output_level_.store(std::sqrt(sum / static_cast<float>(frames)));
}

void AudioEngine::feed_packet(const uint8_t* data, size_t len, float peer_volume) {
    if (!running_ || !decoder_) {
        return;
    }

    const int
        samples = opus_decode_float(decoder_.get(), data, static_cast<int>(len), decode_buf_.data(), FRAME_SIZE, 0);

    if (samples < 0) {
        LOG_ERROR() << "opus_decode_float failed: " << opus_strerror(samples);
        return;
    }

    if (samples > 0) {
        if (peer_volume != 1.0f) {
            for (int i = 0; i < samples * CHANNELS; ++i) {
                decode_buf_[static_cast<size_t>(i)] *= peer_volume;
            }
        }
        playback_ring_.write(decode_buf_.data(), static_cast<size_t>(samples));
    }
}
