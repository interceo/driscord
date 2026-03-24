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
    if (!running_ || !on_packet_ || muted_) {
        return;
    }

    float sum = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        sum += input[i] * input[i];
    }
    input_level_.store(std::sqrt(sum / static_cast<float>(frames)));

    uint32_t consumed = 0;
    while (consumed < frames) {
        uint32_t to_copy =
            std::min(static_cast<uint32_t>(opus::kFrameSize - capture_pos_), frames - consumed);
        std::memcpy(&capture_buf_[capture_pos_], &input[consumed], to_copy * sizeof(float));
        capture_pos_ += to_copy;
        consumed += to_copy;

        if (capture_pos_ == static_cast<size_t>(opus::kFrameSize)) {
            uint8_t* opus_start = encode_buf_.data() + protocol::AudioHeader::kWireSize;
            int bytes =
                encoder_
                    ->encode(capture_buf_.data(), opus::kFrameSize, opus_start, opus::kMaxPacket);
            if (bytes > 0) {
                const protocol::AudioHeader ah{.seq = send_seq_++, .sender_ts = WallNow()};
                ah.serialize(encode_buf_.data());
                on_packet_(
                    encode_buf_.data(),
                    protocol::AudioHeader::kWireSize + static_cast<size_t>(bytes)
                );
            }
            capture_pos_ = 0;
        }
    }
}

std::atomic<int> AudioReceiver::next_id_ = 0;

// ---------------------------------------------------------------------------
// PeerBuffer
// ---------------------------------------------------------------------------

AudioReceiver::PeerBuffer::PeerBuffer(utils::Duration buf_delay, int channels, int sample_rate)
    : jitter(buf_delay) {
    decoder.init(sample_rate, channels);
    decode_buf.resize(static_cast<size_t>(opus::kFrameSize) * channels);
    if (channels > 1) {
        mono_buf.resize(opus::kFrameSize);
    }
}

// ---------------------------------------------------------------------------
// AudioReceiver
// ---------------------------------------------------------------------------

AudioReceiver::AudioReceiver(int jitter_ms, int channels, int sample_rate)
    : buf_delay_(std::chrono::milliseconds(jitter_ms))
    , channels_(channels)
    , sample_rate_(sample_rate)
    , id_(next_id_++) {}

std::shared_ptr<AudioReceiver::PeerBuffer>
AudioReceiver::get_or_create_peer(const std::string& peer_id) {
    std::scoped_lock lk(peer_mutex_);
    auto& entry = peer_buffers_[peer_id];
    if (!entry) {
        entry = std::make_shared<PeerBuffer>(buf_delay_, channels_, sample_rate_);
    }
    return entry;
}

void AudioReceiver::do_push(PeerBuffer& pb, const utils::vector_view<const uint8_t> data) {
    if (data.size() <= protocol::AudioHeader::kWireSize) {
        return;
    }

    const auto ah            = protocol::AudioHeader::deserialize(data.data());
    const uint8_t* opus_data = data.data() + protocol::AudioHeader::kWireSize;
    const int opus_len       = static_cast<int>(data.size() - protocol::AudioHeader::kWireSize);

    const int samples =
        pb.decoder.decode(opus_data, opus_len, pb.decode_buf.data(), opus::kFrameSize);
    if (samples <= 0) {
        LOG_ERROR()
            << "[audio-recv/" << id_ << "] decode failed seq=" << ah.seq
            << " opus_len=" << opus_len << " result=" << samples;
        return;
    }

    if (channels_ > 1) {
        for (int i = 0; i < samples; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels_; ++ch) {
                sum += pb.decode_buf[static_cast<size_t>(i) * channels_ + ch];
            }
            pb.mono_buf[static_cast<size_t>(i)] = sum / channels_;
        }
        std::vector<float> pcm(pb.mono_buf.begin(), pb.mono_buf.begin() + samples);
        pb.jitter.push(PcmFrame{.samples = std::move(pcm), .sender_ts = ah.sender_ts});
    } else {
        std::vector<float> pcm(pb.decode_buf.begin(), pb.decode_buf.begin() + samples);
        pb.jitter.push(PcmFrame{.samples = std::move(pcm), .sender_ts = ah.sender_ts});
    }

    pb.last_packet = utils::Now();
    ++pb.push_count;
    if (pb.push_count == 1) {
        LOG_INFO() << "[audio-recv/" << id_ << "] peer first push seq=" << ah.seq;
    } else if (pb.push_count % 30 == 0) {
        const auto st = pb.jitter.stats();
        LOG_INFO()
            << "[audio-recv/" << id_ << "] push#" << pb.push_count
            << " queue=" << st.queue_size << " drops=" << st.drop_count
            << " misses=" << st.miss_count;
    }
}

void AudioReceiver::push_packet(
    const std::string& peer_id,
    const utils::vector_view<const uint8_t> data
) {
    auto pb = get_or_create_peer(peer_id);
    do_push(*pb, data);
}

std::vector<float> AudioReceiver::pop() {
    std::vector<std::shared_ptr<PeerBuffer>> peers;
    {
        std::scoped_lock lk(peer_mutex_);
        peers.reserve(peer_buffers_.size());
        for (auto& [_, pb] : peer_buffers_) peers.push_back(pb);
    }

    std::vector<float> mix;
    for (auto& pb : peers) {
        auto frame = pb->jitter.pop();
        if (!frame || frame->samples.empty()) continue;
        if (mix.empty()) {
            mix = std::move(frame->samples);
        } else {
            const size_t n = std::min(mix.size(), frame->samples.size());
            for (size_t i = 0; i < n; ++i) mix[i] += frame->samples[i];
        }
    }

    if (mix.empty()) return {};
    ++pop_count_;
    return mix;
}

size_t AudioReceiver::evict_old(utils::Duration max_delay) {
    std::vector<std::shared_ptr<PeerBuffer>> peers;
    {
        std::scoped_lock lk(peer_mutex_);
        for (auto& [_, pb] : peer_buffers_) peers.push_back(pb);
    }
    size_t total = 0;
    for (auto& pb : peers) total += pb->jitter.evict_old(max_delay);
    return total;
}

bool AudioReceiver::primed() const {
    std::scoped_lock lk(peer_mutex_);
    for (auto& [_, pb] : peer_buffers_) {
        if (pb->jitter.primed()) return true;
    }
    return false;
}

std::optional<utils::WallTimestamp> AudioReceiver::front_effective_ts() const {
    std::scoped_lock lk(peer_mutex_);
    std::optional<utils::WallTimestamp> earliest;
    for (auto& [_, pb] : peer_buffers_) {
        auto ts = pb->jitter.front_effective_ts();
        if (ts && (!earliest || *ts < *earliest)) earliest = ts;
    }
    return earliest;
}

int64_t AudioReceiver::front_age_ms() const {
    std::scoped_lock lk(peer_mutex_);
    int64_t oldest = -1;
    for (auto& [_, pb] : peer_buffers_) {
        auto age = pb->jitter.front_age_ms();
        if (age >= 0 && (oldest < 0 || age > oldest)) oldest = age;
    }
    return oldest;
}

void AudioReceiver::reset() {
    std::scoped_lock lk(peer_mutex_);
    peer_buffers_.clear();
}

AudioReceiver::Stats AudioReceiver::stats() const {
    std::scoped_lock lk(peer_mutex_);
    Stats agg{};
    for (auto& [peer_id, pb] : peer_buffers_) {
        auto s = pb->jitter.stats();
        agg.queue_size += s.queue_size;
        agg.drop_count += s.drop_count;
        agg.miss_count += s.miss_count;
        agg.peers[peer_id] = s;
    }
    return agg;
}
