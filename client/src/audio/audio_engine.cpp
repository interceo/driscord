#define MINIAUDIO_IMPLEMENTATION
#include "audio_engine.hpp"

#include <miniaudio.h>
#include <opus.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "log.hpp"
#include "utils/byte_utils.hpp"

using namespace drist;

void OpusEncoderDeleter::operator()(OpusEncoder* e) const { opus_encoder_destroy(e); }
void OpusDecoderDeleter::operator()(OpusDecoder* d) const { opus_decoder_destroy(d); }

AudioEngine::AudioEngine(int jitter_ms)
    : voice_jitter_(static_cast<size_t>(jitter_ms)),
      screen_jitter_(static_cast<size_t>(jitter_ms)),
      capture_buf_(FRAME_SIZE, 0.0f),
      screen_mix_buf_(FRAME_SIZE, 0.0f),
      encode_buf_(AUDIO_HEADER_SIZE + MAX_OPUS_PACKET),
      decode_buf_(FRAME_SIZE),
      screen_capture_buf_(FRAME_SIZE * SCREEN_AUDIO_CHANNELS, 0.0f),
      screen_encode_buf_(AUDIO_HEADER_SIZE + MAX_OPUS_PACKET),
      screen_decode_buf_(FRAME_SIZE * SCREEN_AUDIO_CHANNELS) {}

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
    voice_jitter_.reset();
    screen_jitter_.reset();
    voice_send_seq_ = 0;

    if (!screen_decoder_) {
        OpusDecoderPtr sdec(opus_decoder_create(SAMPLE_RATE, SCREEN_AUDIO_CHANNELS, &err));
        if (err == OPUS_OK) {
            screen_decoder_ = std::move(sdec);
        } else {
            LOG_ERROR() << "screen opus_decoder_create failed: " << opus_strerror(err);
        }
    }

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
    screen_encoder_.reset();
    screen_decoder_.reset();
    on_screen_audio_packet_ = nullptr;
    voice_jitter_.reset();
    screen_jitter_.reset();

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
            uint8_t* opus_start = encode_buf_.data() + AUDIO_HEADER_SIZE;
            int bytes = opus_encode_float(encoder_.get(), capture_buf_.data(), FRAME_SIZE, opus_start, MAX_OPUS_PACKET);
            if (bytes > 0) {
                write_u16_le(encode_buf_.data(), voice_send_seq_++);
                write_u32_le(encode_buf_.data() + 2, now_ms());
                on_packet_(encode_buf_.data(), AUDIO_HEADER_SIZE + static_cast<size_t>(bytes));
            }
            capture_pos_ = 0;
        }
    }
}

void AudioEngine::on_playback(float* output, const uint32_t frames) {
    if (deafened_) {
        voice_jitter_.pop(output, frames);
        screen_jitter_.pop(output, frames);
        std::memset(output, 0, frames * sizeof(float));
        output_level_.store(0.0f);
        return;
    }

    voice_jitter_.pop(output, frames);

    if (screen_mix_buf_.size() < frames) {
        screen_mix_buf_.resize(frames);
    }
    screen_jitter_.pop(screen_mix_buf_.data(), frames);
    if (!sharing_screen_audio_) {
        for (uint32_t i = 0; i < frames; ++i) {
            output[i] += screen_mix_buf_[i];
        }
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

    if (len <= AUDIO_HEADER_SIZE) {
        return;
    }

    uint16_t seq = read_u16_le(data);
    const uint8_t* opus_data = data + AUDIO_HEADER_SIZE;
    int opus_len = static_cast<int>(len - AUDIO_HEADER_SIZE);

    const int samples = opus_decode_float(decoder_.get(), opus_data, opus_len, decode_buf_.data(), FRAME_SIZE, 0);

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
        voice_jitter_.push(decode_buf_.data(), static_cast<size_t>(samples), seq);
    }
}

bool AudioEngine::init_screen_audio(PacketCallback on_screen_audio_packet) {
    int err;
    OpusEncoderPtr enc(opus_encoder_create(SAMPLE_RATE, SCREEN_AUDIO_CHANNELS, OPUS_APPLICATION_AUDIO, &err));
    if (err != OPUS_OK) {
        LOG_ERROR() << "screen opus_encoder_create failed: " << opus_strerror(err);
        return false;
    }
    opus_encoder_ctl(enc.get(), OPUS_SET_BITRATE(SCREEN_AUDIO_BITRATE));

    OpusDecoderPtr dec(opus_decoder_create(SAMPLE_RATE, SCREEN_AUDIO_CHANNELS, &err));
    if (err != OPUS_OK) {
        LOG_ERROR() << "screen opus_decoder_create failed: " << opus_strerror(err);
        return false;
    }

    screen_encoder_ = std::move(enc);
    screen_decoder_ = std::move(dec);
    on_screen_audio_packet_ = std::move(on_screen_audio_packet);
    screen_capture_pos_ = 0;
    sharing_screen_audio_ = true;

    LOG_INFO() << "screen audio codec initialized";
    return true;
}

void AudioEngine::shutdown_screen_audio() {
    sharing_screen_audio_ = false;
    screen_encoder_.reset();
    on_screen_audio_packet_ = nullptr;
    screen_capture_pos_ = 0;
    screen_send_seq_ = 0;
    screen_jitter_.reset();
    LOG_INFO() << "screen audio codec shut down";
}

void AudioEngine::feed_screen_audio_pcm(const float* samples, size_t frames, int channels) {
    if (!screen_encoder_ || !on_screen_audio_packet_) {
        return;
    }

    size_t consumed = 0;
    const size_t stereo_frame_size = static_cast<size_t>(FRAME_SIZE) * SCREEN_AUDIO_CHANNELS;

    while (consumed < frames) {
        size_t remaining_slots = static_cast<size_t>(FRAME_SIZE) - screen_capture_pos_ / SCREEN_AUDIO_CHANNELS;
        size_t to_copy = std::min(remaining_slots, frames - consumed);

        for (size_t i = 0; i < to_copy; ++i) {
            size_t src_idx = (consumed + i) * channels;
            size_t dst_idx = screen_capture_pos_ + i * SCREEN_AUDIO_CHANNELS;
            screen_capture_buf_[dst_idx] = samples[src_idx];
            screen_capture_buf_[dst_idx + 1] = (channels >= 2) ? samples[src_idx + 1] : samples[src_idx];
        }

        screen_capture_pos_ += to_copy * SCREEN_AUDIO_CHANNELS;
        consumed += to_copy;

        if (screen_capture_pos_ >= stereo_frame_size) {
            uint8_t* opus_start = screen_encode_buf_.data() + AUDIO_HEADER_SIZE;
            int bytes = opus_encode_float(
                screen_encoder_.get(),
                screen_capture_buf_.data(),
                FRAME_SIZE,
                opus_start,
                MAX_OPUS_PACKET
            );
            if (bytes > 0) {
                write_u16_le(screen_encode_buf_.data(), screen_send_seq_++);
                write_u32_le(screen_encode_buf_.data() + 2, now_ms());
                on_screen_audio_packet_(screen_encode_buf_.data(), AUDIO_HEADER_SIZE + static_cast<size_t>(bytes));
            }
            screen_capture_pos_ = 0;
        }
    }
}

void AudioEngine::feed_screen_audio_packet(const uint8_t* data, size_t len) {
    if (!screen_decoder_) {
        return;
    }

    if (len <= AUDIO_HEADER_SIZE) {
        return;
    }

    uint16_t seq = read_u16_le(data);
    const uint8_t* opus_data = data + AUDIO_HEADER_SIZE;
    int opus_len = static_cast<int>(len - AUDIO_HEADER_SIZE);

    const int samples =
        opus_decode_float(screen_decoder_.get(), opus_data, opus_len, screen_decode_buf_.data(), FRAME_SIZE, 0);

    if (samples < 0) {
        LOG_ERROR() << "screen opus_decode_float failed: " << opus_strerror(samples);
        return;
    }

    if (samples <= 0) {
        return;
    }

    for (int i = 0; i < samples; ++i) {
        float l = screen_decode_buf_[static_cast<size_t>(i) * 2];
        float r = screen_decode_buf_[static_cast<size_t>(i) * 2 + 1];
        decode_buf_[static_cast<size_t>(i)] = (l + r) * 0.5f;
    }
    screen_jitter_.push(decode_buf_.data(), static_cast<size_t>(samples), seq);
}
