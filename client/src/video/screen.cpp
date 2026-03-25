#include "screen.hpp"

#include "utils/protocol.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstring>

ScreenSender::~ScreenSender() {
    stop_sharing();
}

bool ScreenSender::start_sharing(
    const CaptureTarget& target,
    const size_t max_w,
    const size_t max_h,
    const size_t fps,
    const size_t bitrate_kbps,
    bool share_audio,
    SendCb on_video,
    SendCb on_screen_audio
) {
    if (!video_sender_.start(fps, bitrate_kbps, std::move(on_video))) {
        return false;
    }

    screen_capture_ = ScreenCapture::create();
    if (!screen_capture_->start(fps, target, max_w, max_h, [this](ScreenCapture::Frame frame) {
            video_sender_.push_frame(std::move(frame));
        })) {
        video_sender_.stop();
        screen_capture_.reset();
        return false;
    }

    if (share_audio && on_screen_audio && SystemAudioCapture::available()) {
        auto enc = std::make_unique<OpusEncode>();
        if (enc->init(
                opus::kSampleRate,
                SystemAudioCapture::kChannels,
                128000,
                2049 /*OPUS_APPLICATION_AUDIO*/
            )) {
            auto cap = SystemAudioCapture::create();
            if (cap && cap->start([this](const float* s, size_t f, int c) {
                    on_audio_captured_(s, f, c);
                })) {
                screen_audio_encoder_ = std::move(enc);
                system_audio_capture_ = std::move(cap);
                on_screen_audio_      = std::move(on_screen_audio);
                screen_audio_buf_.assign(
                    static_cast<size_t>(opus::kFrameSize) * SystemAudioCapture::kChannels,
                    0.0f
                );
                screen_audio_encode_buf_
                    .resize(protocol::AudioHeader::kWireSize + opus::kMaxPacket);
                screen_audio_pos_ = 0;
                screen_audio_seq_ = 0;
            }
        }
    }

    return true;
}

void ScreenSender::stop_sharing() {
    if (system_audio_capture_) {
        system_audio_capture_->stop();
        system_audio_capture_.reset();
    }
    screen_audio_encoder_.reset();
    on_screen_audio_  = nullptr;
    screen_audio_pos_ = 0;
    screen_audio_seq_ = 0;

    if (screen_capture_) {
        screen_capture_->stop();
        screen_capture_.reset();
    }
    video_sender_.stop();
}

void ScreenSender::on_audio_captured_(const float* samples, size_t frames, int channels) {
    if (!screen_audio_encoder_ || !on_screen_audio_) {
        return;
    }
    constexpr int kCh         = SystemAudioCapture::kChannels;
    const size_t stereo_frame = static_cast<size_t>(opus::kFrameSize) * kCh;
    size_t consumed           = 0;
    while (consumed < frames) {
        size_t remaining = static_cast<size_t>(opus::kFrameSize) - screen_audio_pos_ / kCh;
        size_t to_copy   = std::min(remaining, frames - consumed);
        for (size_t i = 0; i < to_copy; ++i) {
            size_t src                 = (consumed + i) * channels;
            size_t dst                 = screen_audio_pos_ + i * kCh;
            screen_audio_buf_[dst]     = samples[src];
            screen_audio_buf_[dst + 1] = (channels >= 2) ? samples[src + 1] : samples[src];
        }
        screen_audio_pos_ += to_copy * kCh;
        consumed += to_copy;
        if (screen_audio_pos_ >= stereo_frame) {
            uint8_t* out = screen_audio_encode_buf_.data() + protocol::AudioHeader::kWireSize;
            int bytes =
                screen_audio_encoder_
                    ->encode(screen_audio_buf_.data(), opus::kFrameSize, out, opus::kMaxPacket);
            if (bytes > 0) {
                protocol::AudioHeader ah{.seq = screen_audio_seq_++, .sender_ts = utils::WallNow()};
                ah.serialize(screen_audio_encode_buf_.data());
                on_screen_audio_(
                    screen_audio_encode_buf_.data(),
                    protocol::AudioHeader::kWireSize + static_cast<size_t>(bytes)
                );
            }
            screen_audio_pos_ = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// ScreenReceiver
// ---------------------------------------------------------------------------

ScreenReceiver::ScreenReceiver(int buffer_ms, int /*max_sync_gap_ms*/)
    : video_recv_(buffer_ms, 0)
    , audio_jitter_ms_(buffer_ms) {}

void ScreenReceiver::push_video_packet(
    const std::string& peer_id,
    const utils::vector_view<const uint8_t> data
) {
    video_recv_.push_video_packet(peer_id, data);
}

void ScreenReceiver::push_audio_packet(
    const std::string& peer_id,
    const utils::vector_view<const uint8_t> data
) {
    std::shared_ptr<AudioReceiver> recv;
    {
        std::scoped_lock lk(audio_mutex_);
        auto it = audio_receivers_.find(peer_id);
        if (it != audio_receivers_.end()) {
            recv = it->second;
        }
    }
    if (recv) {
        recv->push_packet(data);
    }
}

void ScreenReceiver::add_audio_peer(const std::string& peer_id) {
    std::scoped_lock lk(audio_mutex_);
    if (!audio_receivers_.count(peer_id)) {
        audio_receivers_[peer_id] =
            std::make_shared<AudioReceiver>(audio_jitter_ms_, /*channels=*/2);
    }
}

void ScreenReceiver::remove_audio_peer(const std::string& peer_id) {
    std::scoped_lock lk(audio_mutex_);
    audio_receivers_.erase(peer_id);
}

std::shared_ptr<AudioReceiver> ScreenReceiver::audio_receiver(const std::string& peer_id) {
    std::scoped_lock lk(audio_mutex_);
    auto it = audio_receivers_.find(peer_id);
    return it != audio_receivers_.end() ? it->second : nullptr;
}

std::shared_ptr<const AudioReceiver>
ScreenReceiver::audio_receiver(const std::string& peer_id) const {
    std::scoped_lock lk(audio_mutex_);
    auto it = audio_receivers_.find(peer_id);
    return it != audio_receivers_.end() ? it->second : nullptr;
}

void ScreenReceiver::update(std::function<void(const VideoReceiver::Frame&)> on_frame) {
    video_recv_.update(std::move(on_frame));
}

void ScreenReceiver::set_keyframe_callback(std::function<void()> fn) {
    video_recv_.set_keyframe_callback(std::move(fn));
}

std::string ScreenReceiver::active_peer() const {
    return video_recv_.active_peer();
}

bool ScreenReceiver::active() const {
    return video_recv_.active();
}

std::unordered_set<std::string> ScreenReceiver::active_peers() const {
    return video_recv_.active_peers();
}

int ScreenReceiver::measured_kbps() const {
    return video_recv_.measured_kbps();
}

VideoReceiver::Stats ScreenReceiver::video_stats() const {
    return video_recv_.video_stats();
}

AudioReceiver::Stats ScreenReceiver::audio_stats() const {
    std::scoped_lock lk(audio_mutex_);
    AudioReceiver::Stats agg{};
    for (const auto& [_, r] : audio_receivers_) {
        const auto s = r->stats();
        agg.queue_size += s.queue_size;
        agg.drop_count += s.drop_count;
        agg.miss_count += s.miss_count;
    }
    return agg;
}

bool ScreenReceiver::audio_primed() const {
    std::scoped_lock lk(audio_mutex_);
    for (const auto& [_, r] : audio_receivers_) {
        if (r->primed()) return true;
    }
    return false;
}

std::optional<utils::WallTimestamp> ScreenReceiver::audio_front_effective_ts() const {
    std::scoped_lock lk(audio_mutex_);
    std::optional<utils::WallTimestamp> earliest;
    for (const auto& [_, r] : audio_receivers_) {
        auto ts = r->front_effective_ts();
        if (ts && (!earliest || *ts < *earliest)) earliest = ts;
    }
    return earliest;
}

int64_t ScreenReceiver::audio_front_age_ms() const {
    std::scoped_lock lk(audio_mutex_);
    int64_t oldest = -1;
    for (const auto& [_, r] : audio_receivers_) {
        const auto age = r->front_age_ms();
        if (age >= 0 && (oldest < 0 || age > oldest)) oldest = age;
    }
    return oldest;
}

void ScreenReceiver::evict_old(utils::Duration max_delay) {
    const size_t vdrop = video_recv_.evict_old(max_delay);
    size_t adrop = 0;
    {
        std::scoped_lock lk(audio_mutex_);
        for (const auto& [_, r] : audio_receivers_) {
            adrop += r->evict_old(max_delay);
        }
    }
    if (vdrop > 0 || adrop > 0) {
        LOG_INFO()
            << "[screen-recv] evict_old("
            << std::chrono::duration_cast<std::chrono::milliseconds>(max_delay).count() << "ms)"
            << " video=" << vdrop << " audio=" << adrop;
    }
}

void ScreenReceiver::reset() {
    video_recv_.reset();
    std::scoped_lock lk(audio_mutex_);
    audio_receivers_.clear();
}

void ScreenReceiver::reset_audio() {
    std::scoped_lock lk(audio_mutex_);
    for (const auto& [_, r] : audio_receivers_) {
        r->reset();
    }
}
