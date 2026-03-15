#define MINIAUDIO_IMPLEMENTATION
#include "audio_sender.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "log.hpp"
#include "utils/byte_utils.hpp"

using namespace utils;

AudioSender::AudioSender() = default;
AudioSender::~AudioSender() { stop(); }

bool AudioSender::start(PacketCallback on_packet) {
    if (running_) {
        return true;
    }

    auto enc = std::make_unique<OpusEncode>();
    if (!enc->init(opus::kSampleRate, kChannels, 64000, 2048 /* OPUS_APPLICATION_VOIP */)) {
        LOG_ERROR() << "AudioSender: failed to init Opus encoder";
        return false;
    }

    auto dev = std::make_unique<ma_device>();
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = kChannels;
    config.sampleRate = opus::kSampleRate;
    config.dataCallback = [](ma_device* d, void* /*out*/, const void* in, ma_uint32 fc) {
        static_cast<AudioSender*>(d->pUserData)->on_capture(static_cast<const float*>(in), fc);
    };
    config.pUserData = this;
    config.periodSizeInFrames = opus::kFrameSize;

    if (ma_device_init(nullptr, &config, dev.get()) != MA_SUCCESS) {
        LOG_ERROR() << "AudioSender: ma_device_init failed";
        return false;
    }
    if (ma_device_start(dev.get()) != MA_SUCCESS) {
        LOG_ERROR() << "AudioSender: ma_device_start failed";
        ma_device_uninit(dev.get());
        return false;
    }

    on_packet_ = std::move(on_packet);
    capture_buf_.assign(opus::kFrameSize, 0.0f);
    encode_buf_.resize(protocol::AudioHeader::kWireSize + opus::kMaxPacket);
    capture_pos_ = 0;
    send_seq_ = 0;
    encoder_ = std::move(enc);
    device_ = std::move(dev);
    running_ = true;

    LOG_INFO() << "AudioSender: started";
    return true;
}

void AudioSender::stop() {
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
    LOG_INFO() << "AudioSender: stopped";
}

void AudioSender::on_capture(const float* input, uint32_t frames) {
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
            uint8_t* opus_start = encode_buf_.data() + protocol::AudioHeader::kWireSize;
            int bytes = encoder_->encode(capture_buf_.data(), opus::kFrameSize, opus_start, opus::kMaxPacket);
            if (bytes > 0) {
                const protocol::AudioHeader ah{.seq = send_seq_++, .sender_ts = WallNow()};
                ah.serialize(encode_buf_.data());
                on_packet_(encode_buf_.data(), protocol::AudioHeader::kWireSize + static_cast<size_t>(bytes));
            }
            capture_pos_ = 0;
        }
    }
}
