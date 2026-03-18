#define MINIAUDIO_IMPLEMENTATION
#include "audio.hpp"

#include "log.hpp"
#include "utils/ma_device.hpp"
#include "utils/protocol.hpp"
#include "utils/time.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace utils;

AudioSender::AudioSender() = default;
AudioSender::~AudioSender() {
    stop();
}

bool AudioSender::start(PacketCallback on_packet) {
    if (running_) {
        return true;
    }

    auto enc = std::make_unique<OpusEncode>();
    if (!enc->init(opus::kSampleRate, kChannels, 64000, 2048 /* OPUS_APPLICATION_VOIP */)) {
        LOG_ERROR() << "AudioSender: failed to init Opus encoder";
        return false;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format   = ma_format_f32;
    config.capture.channels = kChannels;
    config.sampleRate       = opus::kSampleRate;
    config.dataCallback     = [](ma_device* d, void* /*out*/, const void* in, ma_uint32 fc) {
        static_cast<AudioSender*>(d->pUserData)->on_capture(static_cast<const float*>(in), fc);
    };
    config.notificationCallback = [](const ma_device_notification* n) {
        auto* self = static_cast<AudioSender*>(n->pDevice->pUserData);
        if (n->type == ma_device_notification_type_stopped && self->running_.load()) {
            LOG_WARNING() << "AudioSender: capture device stopped unexpectedly, restarting";
            ma_device_start(n->pDevice);
        } else if (n->type == ma_device_notification_type_rerouted) {
            LOG_INFO() << "AudioSender: capture device rerouted";
        }
    };
    config.pUserData          = this;
    config.periodSizeInFrames = opus::kFrameSize;

    auto dev = std::make_unique<MaDevice>();
    if (!dev->start(config)) {
        LOG_ERROR() << "AudioSender: failed to start audio device";
        return false;
    }

    on_packet_ = std::move(on_packet);
    capture_buf_.assign(opus::kFrameSize, 0.0f);
    encode_buf_.resize(protocol::AudioHeader::kWireSize + opus::kMaxPacket);
    capture_pos_ = 0;
    send_seq_    = 0;
    encoder_     = std::move(enc);
    device_      = std::move(dev);
    running_     = true;

    LOG_INFO() << "AudioSender: started";
    return true;
}

void AudioSender::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    device_.reset(); // MaDevice destructor calls ma_device_stop + ma_device_uninit
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
            int bytes           = encoder_->encode(capture_buf_.data(), opus::kFrameSize, opus_start, opus::kMaxPacket);
            if (bytes > 0) {
                const protocol::AudioHeader ah{.seq = send_seq_++, .sender_ts = WallNow()};
                ah.serialize(encode_buf_.data());
                on_packet_(encode_buf_.data(), protocol::AudioHeader::kWireSize + static_cast<size_t>(bytes));
            }
            capture_pos_ = 0;
        }
    }
}

int AudioReceiver::next_id_ = 0;

AudioReceiver::AudioReceiver(int jitter_ms, int channels, int sample_rate)
    : jitter_(std::chrono::milliseconds(jitter_ms))
    , channels_(channels)
    , id_(next_id_++) {
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

    const auto ah            = protocol::AudioHeader::deserialize(data);
    const uint8_t* opus_data = data + protocol::AudioHeader::kWireSize;
    int opus_len             = static_cast<int>(len - protocol::AudioHeader::kWireSize);

    const int samples = decoder_.decode(opus_data, opus_len, decode_buf_.data(), opus::kFrameSize);
    if (samples <= 0) {
        LOG_ERROR()
            << "[audio-recv/" << id_ << "] decode failed seq=" << ah.seq << " opus_len=" << opus_len
            << " result=" << samples;
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
        jitter_.push(
            PcmFrame{
                .samples   = std::move(pcm),
                .sender_ts = ah.sender_ts,
            }
        );
    } else {
        if (vol != 1.0f) {
            for (int i = 0; i < samples; ++i) {
                decode_buf_[static_cast<size_t>(i)] *= vol;
            }
        }

        std::vector<float> pcm(decode_buf_.begin(), decode_buf_.begin() + samples);
        jitter_.push(
            PcmFrame{
                .samples   = std::move(pcm),
                .sender_ts = ah.sender_ts,
            }
        );
    }

    ++push_count_;
    if (push_count_ == 1) {
        LOG_INFO() << "[audio-recv/" << id_ << "] first push seq=" << ah.seq << " queue=" << jitter_.queue_size();
    } else if (push_count_ % 30 == 0) {
        const auto st = jitter_.stats();
        LOG_INFO()
            << "[audio-recv/" << id_ << "] push#" << push_count_ << " queue=" << st.queue_size
            << " drops=" << st.drop_count << " misses=" << st.miss_count;
    }
}

std::vector<float> AudioReceiver::pop() {
    auto result = jitter_.pop().samples;
    ++pop_count_;
    if (pop_count_ % 60 == 0) {
        LOG_INFO()
            << "[audio-recv/" << id_ << "] pop#" << pop_count_ << " got=" << (result.empty() ? "null" : "frame")
            << " queue=" << jitter_.queue_size();
    }
    return result;
}
