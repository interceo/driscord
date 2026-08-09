#define MINIAUDIO_IMPLEMENTATION
#include "audio.hpp"

#include "config.hpp"
#include "enum_strings.hpp"
#include "log.hpp"
#include "utils/ma_device.hpp"
#include "utils/mono_clock.hpp"
#include "utils/protocol.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>

using namespace utils;

namespace {

// The ring holds only the mismatch between an Opus frame and the device
// period, so both directions are short bulk moves over a boost::circular_buffer.
size_t ring_write(boost::circular_buffer<float>& ring, const float* src, size_t n)
{
    n = std::min(n, ring.reserve());
    ring.insert(ring.end(), src, src + n);
    return n;
}

size_t ring_read(boost::circular_buffer<float>& ring, float* dst, size_t n)
{
    n = std::min(n, ring.size());
    std::copy_n(ring.begin(), n, dst);
    ring.erase_begin(n);
    return n;
}

} // namespace

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
        in_silence_ = true;
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
        in_silence_ = true;
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
                const protocol::AudioHeader ah {
                    .seq = send_seq_++,
                    .flags = in_silence_ ? protocol::flags::kTalkspurtStart : 0u,
                    .sender_ts_us = utils::MonoClock::now_us(),
                };
                in_silence_ = false;
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

AudioReceiver::AudioReceiver(std::shared_ptr<avsync::MediaClock> clock,
    const int channels,
    const int sample_rate,
    const utils::TimeSource& time)
    : clock_(std::move(clock))
    , time_(&time)
    , wsola_(sample_rate, 1)
    , ring_(kRingCapacity)
    , channels_(std::max(1, channels))
    , sample_rate_(sample_rate)
    , frame_duration_us_(static_cast<int64_t>(opus::kFrameSize) * 1'000'000 / sample_rate)
    , id_(next_id_++)
{
    decoder_.init(sample_rate, channels_);
    decode_buf_.resize(static_cast<size_t>(opus::kFrameSize) * static_cast<size_t>(channels_));
    mono_buf_.resize(opus::kFrameSize);
    stage_.reserve(kStageCapacity);
    scratch_.resize(kStageCapacity);
}

AudioReceiver::~AudioReceiver() = default;

// --- network thread ---

void AudioReceiver::push_packet(const std::span<const uint8_t> data)
{
    const auto ah = protocol::AudioHeader::deserialize({ data.data(), data.size() });
    if (!ah) {
        return;
    }
    const size_t opus_len = data.size() - protocol::AudioHeader::kWireSize;
    if (opus_len == 0) {
        return;
    }

    packets_received_.inc();

    if (opus_len > kMaxStoredPacket) [[unlikely]] {
        // Storing packets inline is what keeps this path allocation-free; a
        // packet this large is not something the sender should ever produce.
        drop_count_.inc();
        LOG_WARNING() << "[audio-recv/" << id_ << "] oversized packet "
                      << opus_len << "B dropped";
        return;
    }

    clock_->observe(avsync::MediaClock::Stream::Audio, ah->sender_ts_us,
        time_->now_us());

    Packet pkt;
    pkt.len = static_cast<uint16_t>(opus_len);
    pkt.flags = ah->flags;
    pkt.sender_ts_us = ah->sender_ts_us;
    std::memcpy(pkt.data.data(), data.data() + protocol::AudioHeader::kWireSize, opus_len);

    std::scoped_lock lk(buffer_lock_);
    if (buffer_.push(ah->seq, std::move(pkt)) != utils::PushResult::Stored) {
        drop_count_.inc();
    }
}

// --- audio callback ---

size_t AudioReceiver::read(float* out, const size_t frames)
{
    if (reset_pending_.exchange(false, std::memory_order_acq_rel)) [[unlikely]] {
        decoder_.reset_state();
        ring_.clear();
        stage_.clear();
        primed_ = false;
        have_held_ = false;
        held_ts_us_ = 0;
        consecutive_conceals_ = 0;
        pending_decoder_reset_ = false;
    }

    // A step can emit two frames plus an inserted pitch period, so stop
    // pulling once there may not be room for another one.
    while (ring_.size() < frames && ring_.reserve() >= kStageCapacity) {
        if (!playout_step()) {
            break;
        }
    }

    const size_t got = ring_read(ring_, out, frames);
    total_samples_out_.inc(frames);
    if (got < frames) {
        std::fill(out + got, out + frames, 0.0f);
        silence_samples_.inc(frames - got);
        if (primed_) {
            underrun_count_.inc();
        }
    }
    return got;
}

bool AudioReceiver::playout_step()
{
    const int64_t now = time_->now_us();

    if (!primed_) {
        // Hold the stream back until its first packet is actually due. This is
        // what establishes the playout delay, and — unlike the buffer this
        // replaces — it happens again after every underrun, so a stream that
        // stalls does not spend the rest of the call running on empty.
        // Wait for the clock to have something to say. Starting before it does
        // means starting at an arbitrary position and then spending seconds
        // time-stretching back to the right one — during which this stream is
        // measurably out of step with the peer's video. A few hundred
        // milliseconds of silence at the start of a call costs nothing.
        if (!clock_->ready()) {
            return false;
        }
        if (wait_for_video_.load(std::memory_order_relaxed)
            && !clock_->stream_ready(avsync::MediaClock::Stream::Video)) {
            return false;
        }

        std::scoped_lock lk(buffer_lock_);
        auto first = buffer_.next_present();
        if (!first) {
            return false;
        }

        if (clock_->deadline_us(buffer_.peek(*first)->sender_ts_us) > now) {
            return false; // nothing is due yet
        }

        // Start at the newest packet that is already due, not the oldest one
        // still lying around. Anything before it is backlog — starting there
        // would begin playback that much behind schedule and leave the stream
        // out of step with the peer's video until time-stretching crawled it
        // back.
        while (true) {
            const auto next = buffer_.next_present(*first + 1);
            if (!next
                || clock_->deadline_us(buffer_.peek(*next)->sender_ts_us) > now) {
                break;
            }
            first = next;
        }

        const Packet* p = buffer_.peek(*first);
        if (blocked_by_video(p->sender_ts_us)) {
            return false;
        }
        buffer_.advance_base_to(*first);
        next_ts_us_ = p->sender_ts_us;
        primed_ = true;
        consecutive_conceals_ = 0;
        // Whatever was held belonged to the old playout position.
        have_held_ = false;
    }

    // Where playback has got to, relative to where the shared clock says it
    // should be. `lead > 0` means the audio about to be played is newer than
    // the schedule calls for — playback has outrun it and must slow down;
    // `lead < 0` means it has fallen behind and must catch up. Both streams
    // from this peer measure against the same clock, which is what keeps them
    // together, and the sound card's rate is never exactly the sender's, so
    // this correction runs for the whole call rather than just at startup.
    const int64_t emit_ts_us = have_held_ ? held_ts_us_ : next_ts_us_;
    const int64_t lead = clock_->deadline_us(emit_ts_us) - now;
    actual_delay_us_ = clock_->target_delay_us() - lead;

    if (blocked_by_video(emit_ts_us)) {
        return false;
    }

    if (clock_->ready() && std::llabs(lead) > sync_defaults::kResyncThresholdUs)
        [[unlikely]] {
        // Too far out to correct gradually. Ahead of schedule this means
        // holding — emitting silence costs nothing — and behind it means
        // re-priming, which drops the stale audio in the way.
        resync_count_.inc();
        if (lead < 0) {
            primed_ = false;
        }
        return false;
    }

    // The similarity search needs more signal than one 20 ms frame provides,
    // so playback runs one frame behind the decoder: `held_` is what goes out
    // now, and the frame after it supplies the context the search needs.
    // Consuming exactly one packet per step is what keeps the read cursor from
    // outrunning arrivals — pulling a second packet to feed the search would
    // punch a hole in a stream that was never lossy.
    if (!have_held_) {
        if (!decode_into(held_, &held_ts_us_)) {
            return false;
        }
        have_held_ = true;
    }

    refill_stretch_budget(now);
    const bool may_stretch = stretch_budget_us_ > 0 && clock_->ready();
    const bool expand = may_stretch && lead > sync_defaults::kBufferHysteresisUs;
    const bool compress = may_stretch && lead < -sync_defaults::kBufferHysteresisUs;

    // Correcting needs the frame after this one, and only a packet that has
    // genuinely arrived will do. Pulling the cursor forward over a packet that
    // is merely in flight would conceal a loss that never happened — and then
    // reject the packet when it turned up. The frame read ahead is kept, so
    // the next step emits it without consuming anything new: over the pair,
    // one packet in, one frame out.
    bool corrected = false;
    if ((compress || expand) && next_packet_ready()) {
        int64_t pending_ts_us = 0;
        if (decode_into(pending_, &pending_ts_us)) {
            stage_.assign(held_.begin(), held_.end());
            stage_.insert(stage_.end(), pending_.begin(), pending_.end());

            const size_t n = compress
                ? wsola_.compress(stage_.data(), stage_.size(), scratch_.data())
                : wsola_.expand(stage_.data(), stage_.size(), scratch_.data(), scratch_.size());

            // The splice lands in the first half, so what belongs to `held_`
            // is everything the pair produced beyond the untouched frame that
            // follows it.
            if (n > pending_.size()) {
                const size_t emitted = n - pending_.size();
                stretch_budget_us_ += std::llabs(static_cast<int64_t>(emitted)
                                          - static_cast<int64_t>(held_.size()))
                    * -1'000'000 / sample_rate_;
                stretch_count_.inc();
                stretch_in_samples_.inc(held_.size());
                stretch_out_samples_.inc(emitted);
                clock_->set_stream_playout_ts(
                    avsync::MediaClock::Stream::Audio, held_ts_us_);
                ring_write(ring_, scratch_.data(), emitted);
                corrected = true;
            }
            held_.swap(pending_); // keep the look-ahead frame for the next step
            held_ts_us_ = pending_ts_us;
        }
    }

    if (!corrected) {
        clock_->set_stream_playout_ts(
            avsync::MediaClock::Stream::Audio, held_ts_us_);
        ring_write(ring_, held_.data(), held_.size());
        have_held_ = false;
        held_ts_us_ = 0;
    }
    return true;
}

bool AudioReceiver::next_packet_ready() const
{
    std::scoped_lock lk(buffer_lock_);
    return buffer_.contains(buffer_.base_seq());
}

bool AudioReceiver::blocked_by_video(const int64_t sender_ts_us) const
{
    if (!wait_for_video_.load(std::memory_order_relaxed)) {
        return false;
    }
    const int64_t video_ts = clock_->stream_playout_ts(
        avsync::MediaClock::Stream::Video);
    if (video_ts <= 0) {
        return true;
    }
    return sender_ts_us - video_ts > sync_defaults::kMaxScreenAudioLeadUs;
}

bool AudioReceiver::decode_into(std::vector<float>& dst, int64_t* sender_ts_us)
{
    // Everything that touches the shared buffer happens here, and the packet
    // bytes are copied out rather than decoded in place: holding the lock
    // across an Opus decode would stall the network thread for the duration of
    // every frame.
    enum class Action { Decode,
        Fec,
        Conceal,
        Stop };
    Action action = Action::Stop;
    uint16_t len = 0;
    int64_t frame_ts_us = 0;

    {
        std::scoped_lock lk(buffer_lock_);
        const uint64_t seq = buffer_.base_seq();

        if (const Packet* pkt = buffer_.peek(seq)) {
            action = Action::Decode;
            len = pkt->len;
            std::memcpy(codec_in_.data(), pkt->data.data(), len);
            frame_ts_us = pkt->sender_ts_us;
            next_ts_us_ = pkt->sender_ts_us;
            buffer_.advance_base_to(seq + 1);
        } else {
            const auto next = buffer_.next_present();
            frame_ts_us = next_ts_us_;

            // A talkspurt boundary is deliberate silence, not loss. Concealing
            // across it would paint Opus's idea of speech over a pause the
            // speaker actually took.
            if (next && (buffer_.peek(*next)->flags & protocol::flags::kTalkspurtStart)) {
                buffer_.advance_base_to(*next);
                primed_ = false;
                pending_decoder_reset_ = true;
                return false;
            }

            if (consecutive_conceals_ >= kMaxConsecutiveConceals) {
                // The stream is gone, not merely lossy. Stop inventing audio
                // and wait for it to come back, re-priming when it does.
                primed_ = false;
                return false;
            }

            if (next && *next == seq + 1) {
                // Opus carries a low-bitrate copy of the previous frame inside
                // the next one. Since that packet is already here, the lost
                // frame can be recovered rather than approximated — the encoder
                // has always paid for this, and nothing used to claim it.
                const Packet* n = buffer_.peek(*next);
                action = Action::Fec;
                len = n->len;
                std::memcpy(codec_in_.data(), n->data.data(), len);
            } else {
                action = Action::Conceal;
            }
            buffer_.advance_base_to(seq + 1);
            next_ts_us_ += frame_duration_us_;
        }
    }

    if (sender_ts_us) {
        *sender_ts_us = frame_ts_us;
    }

    if (pending_decoder_reset_) {
        decoder_.reset_state();
        pending_decoder_reset_ = false;
    }

    int samples = 0;
    switch (action) {
    case Action::Decode:
        samples = decoder_.decode(codec_in_.data(), len,
            decode_buf_.data(), opus::kFrameSize);
        if (samples <= 0) {
            decode_error_count_.inc();
            return false;
        }
        consecutive_conceals_ = 0;
        break;
    case Action::Fec:
        samples = decoder_.decode_fec(codec_in_.data(), len,
            decode_buf_.data(), opus::kFrameSize);
        if (samples > 0) {
            fec_count_.inc();
            fec_samples_.inc(static_cast<uint64_t>(samples));
        }
        [[fallthrough]];
    case Action::Conceal:
        if (samples <= 0) {
            samples = decoder_.decode_plc(decode_buf_.data(), opus::kFrameSize);
            if (samples <= 0) {
                return false;
            }
            conceal_count_.inc();
            conceal_samples_.inc(static_cast<uint64_t>(samples));
        }
        ++consecutive_conceals_;
        break;
    case Action::Stop:
        return false;
    }

    const float* src = decode_buf_.data();
    if (channels_ > 1) {
        for (int i = 0; i < samples; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels_; ++ch) {
                sum += decode_buf_[static_cast<size_t>(i) * static_cast<size_t>(channels_)
                    + static_cast<size_t>(ch)];
            }
            mono_buf_[static_cast<size_t>(i)] = sum / static_cast<float>(channels_);
        }
        src = mono_buf_.data();
    }

    dst.assign(src, src + samples);
    return true;
}

void AudioReceiver::refill_stretch_budget(const int64_t now)
{
    if (budget_updated_us_ == 0) {
        budget_updated_us_ = now;
        return;
    }
    const int64_t elapsed = now - budget_updated_us_;
    if (elapsed <= 0) {
        return;
    }
    budget_updated_us_ = now;
    stretch_budget_us_ = std::min(kMaxStretchBudgetUs,
        stretch_budget_us_ + elapsed * sync_defaults::kMaxStretchPerSecond / 1000);
}

void AudioReceiver::reset()
{
    reset_pending_.store(true, std::memory_order_release);
    {
        std::scoped_lock lk(buffer_lock_);
        buffer_.reset();
    }
    packets_received_.reset();
    drop_count_.reset();
    conceal_count_.reset();
    fec_count_.reset();
    underrun_count_.reset();
    decode_error_count_.reset();
    stretch_count_.reset();
    resync_count_.reset();
    total_samples_out_.reset();
    conceal_samples_.reset();
    fec_samples_.reset();
    silence_samples_.reset();
    stretch_in_samples_.reset();
    stretch_out_samples_.reset();
}

AudioReceiver::Stats AudioReceiver::stats() const
{
    size_t queued = 0;
    {
        std::scoped_lock lk(buffer_lock_);
        queued = buffer_.size();
    }
    const auto p50 = clock_->stream_delay_percentile_us(
        avsync::MediaClock::Stream::Audio, 50);
    const auto p95 = clock_->stream_delay_percentile_us(
        avsync::MediaClock::Stream::Audio, 95);
    const auto p99 = clock_->stream_delay_percentile_us(
        avsync::MediaClock::Stream::Audio, 99);
    return {
        .queue_size = queued,
        .packets_received = packets_received_.load(),
        .drop_count = drop_count_.load(),
        .conceal_count = conceal_count_.load(),
        .fec_count = fec_count_.load(),
        .underrun_count = underrun_count_.load(),
        .decode_errors = decode_error_count_.load(),
        .stretch_count = stretch_count_.load(),
        .resync_count = resync_count_.load(),
        .total_samples_out = total_samples_out_.load(),
        .conceal_samples = conceal_samples_.load(),
        .fec_samples = fec_samples_.load(),
        .silence_samples = silence_samples_.load(),
        .stretch_in_samples = stretch_in_samples_.load(),
        .stretch_out_samples = stretch_out_samples_.load(),
        .target_delay_ms = clock_->target_delay_us() / 1000,
        .actual_delay_ms = actual_delay_us_ / 1000,
        .p50_delay_ms = p50 >= 0 ? p50 / 1000 : -1,
        .p95_delay_ms = p95 >= 0 ? p95 / 1000 : -1,
        .p99_delay_ms = p99 >= 0 ? p99 / 1000 : -1,
        .delay_samples = clock_->stream_sample_count(
            avsync::MediaClock::Stream::Audio),
        .playout_ts_us = clock_->stream_playout_ts(
            avsync::MediaClock::Stream::Audio),
    };
}
