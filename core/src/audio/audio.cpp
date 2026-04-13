#define MINIAUDIO_IMPLEMENTATION
#include "audio.hpp"

#include "log.hpp"
#include "utils/ma_device.hpp"
#include "utils/protocol.hpp"
#include "utils/time.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace utils;

AudioSender::AudioSender() = default;
AudioSender::~AudioSender()
{
    stop();
}

std::string AudioSender::list_input_devices_json()
{
    ma_context ctx;
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) {
        LOG_ERROR()
            << "AudioSender::list_input_devices_json: ma_context_init failed";
        return "[]";
    }

    ma_device_info* devices = nullptr;
    ma_uint32 count = 0;
    nlohmann::json arr = nlohmann::json::array();

    if (ma_context_get_devices(&ctx, nullptr, nullptr, &devices, &count) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            arr.push_back({ { "id", devices[i].name }, { "name", devices[i].name } });
        }
    } else {
        LOG_ERROR() << "AudioSender::list_input_devices_json: "
                       "ma_context_get_devices failed";
    }

    ma_context_uninit(&ctx);
    return arr.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

utils::Expected<void, AudioError> AudioSender::start(PacketCallback on_packet,
    int bitrate_bps)
{
    if (running_) {
        return { };
    }

    auto enc = std::make_unique<OpusEncode>();
    if (!enc->init(opus::kSampleRate, kChannels, bitrate_bps,
            2048 /* OPUS_APPLICATION_VOIP */)) {
        LOG_ERROR() << "AudioSender: failed to init Opus encoder";
        return utils::Unexpected(AudioError::OpusInitFailed);
    }

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = kChannels;
    config.sampleRate = opus::kSampleRate;
    config.dataCallback = [](ma_device* d, void* /*out*/, const void* in,
                              ma_uint32 fc) {
        static_cast<AudioSender*>(d->pUserData)
            ->on_capture(static_cast<const float*>(in), fc);
    };
    config.notificationCallback = [](const ma_device_notification* n) {
        auto* self = static_cast<AudioSender*>(n->pDevice->pUserData);
        if (n->type == ma_device_notification_type_stopped && self->running_.load()) {
            LOG_WARNING()
                << "AudioSender: capture device stopped unexpectedly, restarting";
            ma_device_start(n->pDevice);
        } else if (n->type == ma_device_notification_type_rerouted) {
            LOG_INFO() << "AudioSender: capture device rerouted";
        }
    };
    config.pUserData = this;
    config.periodSizeInFrames = opus::kFrameSize;

    // If a specific device was requested, find its native device ID by name.
    ma_device_id selected_id { };
    if (!device_id_.empty()) {
        ma_context ctx;
        if (ma_context_init(nullptr, 0, nullptr, &ctx) == MA_SUCCESS) {
            ma_device_info* devs = nullptr;
            ma_uint32 count = 0;
            if (ma_context_get_devices(&ctx, nullptr, nullptr, &devs, &count) == MA_SUCCESS) {
                for (ma_uint32 i = 0; i < count; ++i) {
                    if (device_id_ == devs[i].name) {
                        selected_id = devs[i].id; // copy union before uninit
                        config.capture.pDeviceID = &selected_id;
                        LOG_INFO() << "AudioSender: using device '" << device_id_ << "'";
                        break;
                    }
                }
            }
            ma_context_uninit(&ctx);
        }
        if (!config.capture.pDeviceID) {
            LOG_WARNING() << "AudioSender: device '" << device_id_
                          << "' not found, using default";
        }
    }

    auto dev = std::make_unique<MaDevice>();
    if (!dev->start(config)) {
        LOG_ERROR() << "AudioSender: failed to start audio device";
        return utils::Unexpected(AudioError::SenderDeviceStartFailed);
    }

    on_packet_ = std::move(on_packet);
    bitrate_bps_ = bitrate_bps;
    capture_buf_.assign(opus::kFrameSize, 0.0f);
    encode_buf_.resize(protocol::AudioHeader::kWireSize + opus::kMaxPacket);
    capture_pos_ = 0;
    send_seq_ = 0;
    encoder_ = std::move(enc);
    device_ = std::move(dev);
    running_ = true;

    LOG_INFO() << "AudioSender: started";
    return { };
}

void AudioSender::set_device_id(std::string id)
{
    device_id_ = std::move(id);
    if (running_) {
        auto cb = on_packet_;
        const auto br = bitrate_bps_;
        stop();
        if (auto r = start(cb, br); !r) {
            LOG_ERROR() << "AudioSender: set_device_id restart failed: "
                        << to_string(r.error());
        }
    }
}

void AudioSender::stop()
{
    if (!running_) {
        return;
    }
    running_ = false;
    device_
        .reset(); // MaDevice destructor calls ma_device_stop + ma_device_uninit
    encoder_.reset();
    LOG_INFO() << "AudioSender: stopped";
}

void AudioSender::on_capture(const float* input, uint32_t frames)
{
    if (!running_ || !on_packet_ || muted_) {
        return;
    }

    float sum = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        sum += input[i] * input[i];
    }
    const float rms = std::sqrt(sum / static_cast<float>(frames));
    input_level_.store(rms);

    const float gate = noise_gate_.load(std::memory_order_relaxed);
    if (gate > 0.0f && rms < gate) {
        return;
    }

    uint32_t consumed = 0;
    while (consumed < frames) {
        uint32_t to_copy = std::min(static_cast<uint32_t>(opus::kFrameSize - capture_pos_),
            frames - consumed);
        std::memcpy(&capture_buf_[capture_pos_], &input[consumed],
            to_copy * sizeof(float));
        capture_pos_ += to_copy;
        consumed += to_copy;

        if (capture_pos_ == static_cast<size_t>(opus::kFrameSize)) {
            uint8_t* opus_start = encode_buf_.data() + protocol::AudioHeader::kWireSize;
            int bytes = encoder_->encode(capture_buf_.data(), opus::kFrameSize,
                opus_start, opus::kMaxPacket);
            if (bytes > 0) {
                const protocol::AudioHeader ah { .seq = send_seq_++,
                    .sender_ts = WallNow() };
                ah.serialize(encode_buf_.data());
                on_packet_(encode_buf_.data(), protocol::AudioHeader::kWireSize + static_cast<size_t>(bytes));
            }
            capture_pos_ = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// AudioReceiver
// ---------------------------------------------------------------------------

std::atomic<uint64_t> AudioReceiver::next_id_ = 0;

AudioReceiver::AudioReceiver(int jitter_ms, int channels, int sample_rate)
    : jitter_(std::chrono::milliseconds(jitter_ms))
    , channels_(channels)
    , sample_rate_(sample_rate)
    , id_(next_id_++)
{
    decoder_.init(sample_rate, channels);
    decode_buf_.resize(static_cast<size_t>(opus::kFrameSize) * channels);
    if (channels > 1) {
        mono_buf_.resize(opus::kFrameSize);
    }
}

void AudioReceiver::push_packet(utils::vector_view<const uint8_t> data)
{
    if (data.size() <= protocol::AudioHeader::kWireSize) {
        return;
    }

    const auto ah = protocol::AudioHeader::deserialize(data.data());
    const uint8_t* opus_data = data.data() + protocol::AudioHeader::kWireSize;
    const size_t opus_len = data.size() - protocol::AudioHeader::kWireSize;

    std::vector<uint8_t> opus_bytes(opus_data, opus_data + opus_len);

    if (jitter_.push(ah.seq, OpusFrame { .data = std::move(opus_bytes), .sender_ts = ah.sender_ts }) != PushStatus::Stored) {
        drop_count_.inc();
    }

    ++push_count_;
    if (push_count_ == 1) {
        LOG_INFO() << "[audio-recv/" << id_ << "] first push seq=" << ah.seq;
    } else if (push_count_ % 30 == 0) {
        LOG_INFO() << "[audio-recv/" << id_ << "] push#" << push_count_
                   << " queue=" << jitter_.queue_size() << " drops=" << drop_count_.load()
                   << " misses=" << miss_count_.load();
    }
}

utils::vector_view<const float> AudioReceiver::pop()
{
    if (reset_pending_.exchange(false, std::memory_order_acq_rel)) {
        decoder_.reset_state();
    }

    auto [frame, missed] = jitter_.pop();
    if (missed) {
        miss_count_.inc();
        // PLC: ask Opus decoder to generate a concealment frame.
        const int samples = decoder_.decode_plc(decode_buf_.data(), opus::kFrameSize);
        if (samples > 0) {
            ++pop_count_;
            if (channels_ > 1) {
                for (int i = 0; i < samples; ++i) {
                    float sum = 0.0f;
                    for (int ch = 0; ch < channels_; ++ch) {
                        sum += decode_buf_[static_cast<size_t>(i) * channels_ + ch];
                    }
                    mono_buf_[static_cast<size_t>(i)] = sum / channels_;
                }
                return utils::vector_view<const float>(mono_buf_.data(), samples);
            }
            return utils::vector_view<const float>(decode_buf_.data(), samples);
        }
        return utils::vector_view<const float>(nullptr, 0);
    }
    if (!frame || frame->data.empty()) {
        return utils::vector_view<const float>(nullptr, 0);
    }

    const int samples = decoder_.decode(frame->data.data(),
        static_cast<int>(frame->data.size()),
        decode_buf_.data(), opus::kFrameSize);
    if (samples <= 0) {
        decode_error_count_.inc();
        LOG_ERROR() << "[audio-recv/" << id_ << "] decode failed in pop"
                    << " opus_len=" << frame->data.size() << " result=" << samples;
        return utils::vector_view<const float>(nullptr, 0);
    }

    ++pop_count_;
    if (channels_ > 1) {
        for (int i = 0; i < samples; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels_; ++ch) {
                sum += decode_buf_[static_cast<size_t>(i) * channels_ + ch];
            }
            mono_buf_[static_cast<size_t>(i)] = sum / channels_;
        }
        return utils::vector_view<const float>(mono_buf_.data(), samples);
    }
    return utils::vector_view<const float>(decode_buf_.data(), samples);
}

size_t AudioReceiver::evict_old(utils::Duration max_delay)
{
    const auto n = jitter_.evict_old(max_delay);
    drop_count_.inc(n);
    return n;
}

size_t AudioReceiver::evict_before_sender_ts(utils::WallTimestamp cutoff)
{
    const auto n = jitter_.evict_before_sender_ts(cutoff);
    drop_count_.inc(n);
    return n;
}

int64_t AudioReceiver::median_ow_delay_ms() const
{
    return jitter_.ow_delay_ms();
}

bool AudioReceiver::primed() const
{
    return jitter_.primed();
}

std::optional<utils::WallTimestamp> AudioReceiver::front_effective_ts() const
{
    return jitter_.front_effective_ts();
}

int64_t AudioReceiver::front_age_ms() const
{
    return jitter_.front_age_ms();
}

void AudioReceiver::reset()
{
    reset_pending_.store(true, std::memory_order_release);
    jitter_.reset();
    drop_count_.reset();
    miss_count_.reset();
    decode_error_count_.reset();
    push_count_ = 0;
    pop_count_ = 0;
}

AudioReceiver::Stats AudioReceiver::stats() const
{
    return {
        jitter_.queue_size(),
        drop_count_.load(),
        miss_count_.load(),
        push_count_,
        decode_error_count_.load(),
    };
}
