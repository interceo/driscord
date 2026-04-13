#include "screen.hpp"

#include "log.hpp"
#include "utils/protocol.hpp"

#include <algorithm>
#include <cstring>

ScreenSender::~ScreenSender()
{
    stop_sharing();
}

static int compute_base_bitrate_kbps(int max_w, int max_h, int fps)
{
    if (max_w <= 0 || max_h <= 0) {
        return std::clamp(static_cast<int>(15000.0 * fps / 60.0), 1000, 50000);
    }
    constexpr double kRef = 8000.0;
    const double scale = static_cast<double>(max_w) * max_h / (1920.0 * 1080.0)
        * static_cast<double>(fps) / 60.0;
    return std::clamp(static_cast<int>(kRef * scale), 500, 50000);
}

utils::Expected<void, VideoError> ScreenSender::start_sharing(const ScreenCaptureTarget& target,
    const size_t max_w,
    const size_t max_h,
    const size_t fps,
    bool share_audio,
    SendCb on_video,
    SendCb on_screen_audio)
{
    const int bitrate_kbps = compute_base_bitrate_kbps(
        static_cast<int>(max_w), static_cast<int>(max_h), static_cast<int>(fps));
    if (!video_sender_.start(fps, static_cast<size_t>(bitrate_kbps), std::move(on_video))) {
        return utils::Unexpected(VideoError::VideoSenderFailed);
    }

    screen_capture_ = ScreenCapture::create();
    if (!screen_capture_->start(fps, target, max_w, max_h,
            [this](ScreenCapture::Frame frame) {
                video_sender_.push_frame(std::move(frame));
            })) {
        video_sender_.stop();
        screen_capture_.reset();
        return utils::Unexpected(VideoError::CaptureStartFailed);
    }

    if (share_audio && on_screen_audio && SystemAudioCapture::available()) {
        auto enc = std::make_unique<OpusEncode>();
        if (enc->init(opus::kSampleRate, SystemAudioCapture::kChannels,
                system_audio_bitrate_kbps_ * 1000,
                2049 /*OPUS_APPLICATION_AUDIO*/)) {
            auto cap = SystemAudioCapture::create();
            if (cap && cap->start([this](const float* s, size_t f, int c) {
                    on_audio_captured_(s, f, c);
                })) {
                screen_audio_encoder_ = std::move(enc);
                system_audio_capture_ = std::move(cap);
                on_screen_audio_ = std::move(on_screen_audio);
                screen_audio_buf_.assign(static_cast<size_t>(opus::kFrameSize) * SystemAudioCapture::kChannels,
                    0.0f);
                screen_audio_encode_buf_.resize(protocol::AudioHeader::kWireSize + opus::kMaxPacket);
                screen_audio_pos_ = 0;
                screen_audio_seq_ = 0;
            }
        }
    }

    return { };
}

void ScreenSender::stop_sharing()
{
    if (system_audio_capture_) {
        system_audio_capture_->stop();
        system_audio_capture_.reset();
    }
    screen_audio_encoder_.reset();
    on_screen_audio_ = nullptr;
    screen_audio_pos_ = 0;
    screen_audio_seq_ = 0;

    if (screen_capture_) {
        screen_capture_->stop();
        screen_capture_.reset();
    }
    video_sender_.stop();
}

void ScreenSender::on_audio_captured_(const float* samples,
    size_t frames,
    int channels)
{
    if (!screen_audio_encoder_ || !on_screen_audio_) {
        return;
    }
    constexpr int kCh = SystemAudioCapture::kChannels;
    const size_t stereo_frame = static_cast<size_t>(opus::kFrameSize) * kCh;
    size_t consumed = 0;
    while (consumed < frames) {
        size_t remaining = static_cast<size_t>(opus::kFrameSize) - screen_audio_pos_ / kCh;
        size_t to_copy = std::min(remaining, frames - consumed);
        for (size_t i = 0; i < to_copy; ++i) {
            size_t src = (consumed + i) * channels;
            size_t dst = screen_audio_pos_ + i * kCh;
            screen_audio_buf_[dst] = samples[src];
            screen_audio_buf_[dst + 1] = (channels >= 2) ? samples[src + 1] : samples[src];
        }
        screen_audio_pos_ += to_copy * kCh;
        consumed += to_copy;
        if (screen_audio_pos_ >= stereo_frame) {
            uint8_t* out = screen_audio_encode_buf_.data() + protocol::AudioHeader::kWireSize;
            int bytes = screen_audio_encoder_->encode(
                screen_audio_buf_.data(), opus::kFrameSize, out, opus::kMaxPacket);
            if (bytes > 0) {
                protocol::AudioHeader ah { .seq = screen_audio_seq_++,
                    .sender_ts = utils::WallNow() };
                ah.serialize(screen_audio_encode_buf_.data());
                on_screen_audio_(
                    screen_audio_encode_buf_.data(),
                    protocol::AudioHeader::kWireSize + static_cast<size_t>(bytes));
            }
            screen_audio_pos_ = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// ScreenReceiver
// ---------------------------------------------------------------------------

ScreenReceiver::ScreenReceiver(int buffer_ms, int /*max_sync_gap_ms*/, int audio_jitter_ms)
    : video_buffer_ms_(buffer_ms)
    , audio_jitter_ms_(audio_jitter_ms)
{
}

std::shared_ptr<VideoReceiver> ScreenReceiver::current_video_recv_locked()
    const
{
    auto it = video_receivers_.find(current_video_peer_);
    return it != video_receivers_.end() ? it->second : nullptr;
}

void ScreenReceiver::add_video_peer(const std::string& peer_id)
{
    std::scoped_lock lk(video_mutex_);
    if (!video_receivers_.count(peer_id)) {
        auto recv = std::make_shared<VideoReceiver>(peer_id, video_buffer_ms_);
        if (keyframe_cb_) {
            recv->set_keyframe_callback(keyframe_cb_);
        }
        video_receivers_[peer_id] = std::move(recv);
    }
}

void ScreenReceiver::remove_video_peer(const std::string& peer_id)
{
    std::scoped_lock lk(video_mutex_);
    video_receivers_.erase(peer_id);
    if (current_video_peer_ == peer_id) {
        current_video_peer_.clear();
    }
}

void ScreenReceiver::push_video_packet(
    const std::string& peer_id,
    const utils::vector_view<const uint8_t> data,
    uint64_t frame_id)
{
    std::shared_ptr<VideoReceiver> recv;
    {
        std::scoped_lock lk(video_mutex_);
        auto it = video_receivers_.find(peer_id);
        if (it != video_receivers_.end()) {
            recv = it->second;
            current_video_peer_ = peer_id;
        }
    }
    if (recv) {
        recv->push_video_packet(data, frame_id);
    }
}

void ScreenReceiver::push_audio_packet(
    const std::string& peer_id,
    const utils::vector_view<const uint8_t> data)
{
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

void ScreenReceiver::add_audio_peer(const std::string& peer_id)
{
    std::scoped_lock lk(audio_mutex_);
    if (!audio_receivers_.count(peer_id)) {
        audio_receivers_[peer_id] = std::make_shared<AudioReceiver>(audio_jitter_ms_, /*channels=*/2);
    }
}

void ScreenReceiver::remove_audio_peer(const std::string& peer_id)
{
    std::scoped_lock lk(audio_mutex_);
    audio_receivers_.erase(peer_id);
}

std::shared_ptr<AudioReceiver> ScreenReceiver::audio_receiver(
    const std::string& peer_id)
{
    std::scoped_lock lk(audio_mutex_);
    auto it = audio_receivers_.find(peer_id);
    return it != audio_receivers_.end() ? it->second : nullptr;
}

std::shared_ptr<const AudioReceiver> ScreenReceiver::audio_receiver(
    const std::string& peer_id) const
{
    std::scoped_lock lk(audio_mutex_);
    auto it = audio_receivers_.find(peer_id);
    return it != audio_receivers_.end() ? it->second : nullptr;
}

void ScreenReceiver::update(
    std::function<void(const VideoReceiver::Frame&)> on_frame)
{
    std::vector<std::shared_ptr<VideoReceiver>> receivers;
    {
        std::scoped_lock lk(video_mutex_);
        for (auto& [_, r] : video_receivers_) {
            receivers.push_back(r);
        }
    }
    for (auto& r : receivers) {
        r->update(on_frame);
    }
}

void ScreenReceiver::set_keyframe_callback(std::function<void()> fn)
{
    std::scoped_lock lk(video_mutex_);
    keyframe_cb_ = fn;
    for (auto& [_, recv] : video_receivers_) {
        recv->set_keyframe_callback(fn);
    }
}

std::string ScreenReceiver::active_peer() const
{
    std::scoped_lock lk(video_mutex_);
    return current_video_peer_;
}

bool ScreenReceiver::active() const
{
    std::shared_ptr<VideoReceiver> recv;
    {
        std::scoped_lock lk(video_mutex_);
        recv = current_video_recv_locked();
    }
    return recv && recv->active();
}

std::unordered_set<std::string> ScreenReceiver::active_peers() const
{
    std::scoped_lock lk(video_mutex_);
    std::unordered_set<std::string> result;
    for (const auto& [id, r] : video_receivers_) {
        if (r->active()) {
            result.insert(id);
        }
    }
    return result;
}

int ScreenReceiver::measured_kbps() const
{
    std::shared_ptr<VideoReceiver> recv;
    {
        std::scoped_lock lk(video_mutex_);
        recv = current_video_recv_locked();
    }
    return recv ? recv->measured_kbps() : 0;
}

VideoReceiver::Stats ScreenReceiver::video_stats() const
{
    std::shared_ptr<VideoReceiver> recv;
    {
        std::scoped_lock lk(video_mutex_);
        recv = current_video_recv_locked();
    }
    return recv ? recv->video_stats() : VideoReceiver::Stats { };
}

AudioReceiver::Stats ScreenReceiver::audio_stats() const
{
    std::scoped_lock lk(audio_mutex_);
    AudioReceiver::Stats agg { };
    for (const auto& [_, r] : audio_receivers_) {
        const auto s = r->stats();
        agg.queue_size += s.queue_size;
        agg.drop_count += s.drop_count;
        agg.miss_count += s.miss_count;
        agg.packets_received += s.packets_received;
        agg.decode_errors += s.decode_errors;
    }
    return agg;
}

bool ScreenReceiver::video_primed() const
{
    std::shared_ptr<VideoReceiver> recv;
    {
        std::scoped_lock lk(video_mutex_);
        recv = current_video_recv_locked();
    }
    return recv && recv->primed();
}

bool ScreenReceiver::audio_primed() const
{
    std::scoped_lock lk(audio_mutex_);
    for (const auto& [_, r] : audio_receivers_) {
        if (r->primed()) {
            return true;
        }
    }
    return false;
}

std::optional<utils::WallTimestamp> ScreenReceiver::video_front_effective_ts()
    const
{
    std::shared_ptr<VideoReceiver> recv;
    {
        std::scoped_lock lk(video_mutex_);
        recv = current_video_recv_locked();
    }
    return recv ? recv->front_effective_ts() : std::nullopt;
}

std::optional<utils::WallTimestamp> ScreenReceiver::audio_front_effective_ts()
    const
{
    std::scoped_lock lk(audio_mutex_);
    std::optional<utils::WallTimestamp> earliest;
    for (const auto& [_, r] : audio_receivers_) {
        auto ts = r->front_effective_ts();
        if (ts && (!earliest || *ts < *earliest)) {
            earliest = ts;
        }
    }
    return earliest;
}

utils::Duration ScreenReceiver::video_frame_duration() const
{
    std::shared_ptr<VideoReceiver> recv;
    {
        std::scoped_lock lk(video_mutex_);
        recv = current_video_recv_locked();
    }
    return recv ? recv->front_frame_duration() : utils::Duration { };
}

size_t ScreenReceiver::evict_video_before(utils::WallTimestamp cutoff)
{
    std::vector<std::shared_ptr<VideoReceiver>> receivers;
    {
        std::scoped_lock lk(video_mutex_);
        receivers.reserve(video_receivers_.size());
        for (auto& [_, r] : video_receivers_) {
            receivers.push_back(r);
        }
    }
    size_t total = 0;
    for (auto& r : receivers) {
        total += r->evict_before_sender_ts(cutoff);
    }
    return total;
}

size_t ScreenReceiver::evict_audio_before(utils::WallTimestamp cutoff)
{
    std::vector<std::shared_ptr<AudioReceiver>> receivers;
    {
        std::scoped_lock lk(audio_mutex_);
        receivers.reserve(audio_receivers_.size());
        for (auto& [_, r] : audio_receivers_) {
            receivers.push_back(r);
        }
    }
    size_t total = 0;
    for (auto& r : receivers) {
        total += r->evict_before_sender_ts(cutoff);
    }
    return total;
}

int64_t ScreenReceiver::video_median_ow_delay_ms() const
{
    std::shared_ptr<VideoReceiver> recv;
    {
        std::scoped_lock lk(video_mutex_);
        recv = current_video_recv_locked();
    }
    return recv ? recv->median_ow_delay_ms() : -1;
}

int64_t ScreenReceiver::audio_median_ow_delay_ms() const
{
    std::scoped_lock lk(audio_mutex_);
    int64_t best = -1;
    for (const auto& [_, r] : audio_receivers_) {
        const int64_t m = r->median_ow_delay_ms();
        if (m >= 0 && (best < 0 || m < best)) {
            best = m;
        }
    }
    return best;
}

int64_t ScreenReceiver::video_front_age_ms() const
{
    std::shared_ptr<VideoReceiver> recv;
    {
        std::scoped_lock lk(video_mutex_);
        recv = current_video_recv_locked();
    }
    return recv ? recv->front_age_ms() : -1;
}

int64_t ScreenReceiver::audio_front_age_ms() const
{
    std::scoped_lock lk(audio_mutex_);
    int64_t oldest = -1;
    for (const auto& [_, r] : audio_receivers_) {
        const auto age = r->front_age_ms();
        if (age >= 0 && (oldest < 0 || age > oldest)) {
            oldest = age;
        }
    }
    return oldest;
}

void ScreenReceiver::evict_old(utils::Duration max_delay)
{
    std::vector<std::shared_ptr<VideoReceiver>> vreceivers;
    {
        std::scoped_lock lk(video_mutex_);
        for (auto& [_, r] : video_receivers_) {
            vreceivers.push_back(r);
        }
    }
    size_t vdrop = 0;
    for (auto& r : vreceivers) {
        vdrop += r->evict_old(max_delay);
    }

    size_t adrop = 0;
    {
        std::scoped_lock lk(audio_mutex_);
        for (const auto& [_, r] : audio_receivers_) {
            adrop += r->evict_old(max_delay);
        }
    }
    if (vdrop > 0 || adrop > 0) {
        LOG_INFO() << "[screen-recv] evict_old("
                   << std::chrono::duration_cast<std::chrono::milliseconds>(
                          max_delay)
                          .count()
                   << "ms)"
                   << " video=" << vdrop << " audio=" << adrop;
    }
}

void ScreenReceiver::evict_old_video(utils::Duration max_delay)
{
    std::vector<std::shared_ptr<VideoReceiver>> receivers;
    {
        std::scoped_lock lk(video_mutex_);
        for (auto& [_, r] : video_receivers_) {
            receivers.push_back(r);
        }
    }
    for (auto& r : receivers) {
        r->evict_old(max_delay);
    }
}

void ScreenReceiver::reset()
{
    {
        std::scoped_lock lk(video_mutex_);
        video_receivers_.clear();
        current_video_peer_.clear();
    }
    std::scoped_lock lk(audio_mutex_);
    audio_receivers_.clear();
}

void ScreenReceiver::reset_audio()
{
    std::scoped_lock lk(audio_mutex_);
    for (const auto& [_, r] : audio_receivers_) {
        r->reset();
    }
}
