#include "audio_receiver.hpp"

#include "log.hpp"

AudioReceiver::AudioReceiver(int jitter_ms, int channels, int sample_rate)
    : jitter_(static_cast<size_t>(jitter_ms), sample_rate), channels_(channels) {
    if (!decoder_.init(sample_rate, channels)) {
        LOG_ERROR() << "AudioReceiver: failed to init Opus decoder (ch=" << channels << ")";
    }
    decode_buf_.resize(static_cast<size_t>(opus::kFrameSize) * channels);
    if (channels > 1) {
        mono_buf_.resize(opus::kFrameSize);
    }
}

void AudioReceiver::push_packet(const uint8_t* data, size_t len) {
    if (len <= protocol::AudioHeader::kWireSize) {
        return;
    }

    const auto ah = protocol::AudioHeader::deserialize(data);
    const uint8_t* opus_data = data + protocol::AudioHeader::kWireSize;
    int opus_len = static_cast<int>(len - protocol::AudioHeader::kWireSize);

    const int samples = decoder_.decode(opus_data, opus_len, decode_buf_.data(), opus::kFrameSize);
    if (samples <= 0) {
        return;
    }

    const float vol = volume_.load();

    if (channels_ > 1) {
        for (int i = 0; i < samples; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels_; ++ch) {
                sum += decode_buf_[static_cast<size_t>(i) * channels_ + ch];
            }
            mono_buf_[static_cast<size_t>(i)] = (sum / channels_) * vol;
        }
        std::vector<float> pcm(mono_buf_.begin(), mono_buf_.begin() + samples);
        jitter_.push(std::move(pcm), ah.seq, ah.sender_ts);
    } else {
        if (vol != 1.0f) {
            for (int i = 0; i < samples; ++i) {
                decode_buf_[static_cast<size_t>(i)] *= vol;
            }
        }
        std::vector<float> pcm(decode_buf_.begin(), decode_buf_.begin() + samples);
        jitter_.push(std::move(pcm), ah.seq, ah.sender_ts);
    }
}

std::vector<float> AudioReceiver::pop() { return jitter_.pop(); }
