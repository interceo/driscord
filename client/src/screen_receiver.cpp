#include "screen_receiver.hpp"

#include "log.hpp"
#include "utils/byte_utils.hpp"

#include <cstring>

using namespace drist;

namespace {
constexpr int kStaleSeconds = 3;
}  // namespace

ScreenReceiver::ScreenReceiver(int buffer_ms, int max_sync_gap_ms) : jitter_(buffer_ms, max_sync_gap_ms) {
    auto dec = std::make_unique<OpusDecode>();
    if (dec->init(opus::kSampleRate, kScreenAudioChannels)) {
        opus_decoder_ = std::move(dec);
        audio_decode_buf_.resize(static_cast<size_t>(opus::kFrameSize) * kScreenAudioChannels);
        audio_mono_buf_.resize(opus::kFrameSize);
    }
}

ScreenReceiver::~ScreenReceiver() = default;

void ScreenReceiver::set_keyframe_callback(std::function<void()> fn) { on_keyframe_needed_ = std::move(fn); }

void ScreenReceiver::push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len) {
    if (len <= protocol::kChunkHeaderSize) {
        return;
    }

    uint16_t frame_id = read_u16_le(data + 0);
    uint16_t chunk_idx = read_u16_le(data + 2);
    uint16_t total_chunks = read_u16_le(data + 4);

    if (total_chunks == 0 || chunk_idx >= total_chunks) {
        return;
    }

    const uint8_t* chunk_data = data + protocol::kChunkHeaderSize;
    size_t chunk_len = len - protocol::kChunkHeaderSize;

    std::scoped_lock lk(mutex_);

    current_peer_ = peer_id;
    last_packet_ = std::chrono::steady_clock::now();

    if (frame_id != re_frame_id_ || total_chunks != re_total_) {
        if (re_total_ > 0 && re_got_ < re_total_ && on_keyframe_needed_) {
            on_keyframe_needed_();
        }
        re_frame_id_ = frame_id;
        re_total_ = total_chunks;
        re_got_ = 0;
        re_buf_.clear();
    }

    size_t offset = static_cast<size_t>(chunk_idx) * protocol::kMaxChunkPayload;
    size_t needed = offset + chunk_len;
    if (re_buf_.size() < needed) {
        re_buf_.resize(needed);
    }
    std::memcpy(re_buf_.data() + offset, chunk_data, chunk_len);
    ++re_got_;

    if (re_got_ < total_chunks) {
        return;
    }

    if (re_buf_.size() <= protocol::kVideoHeaderSize) {
        return;
    }

    uint32_t w = read_u32_le(re_buf_.data() + 0);
    uint32_t h = read_u32_le(re_buf_.data() + 4);
    uint32_t sender_ts = read_u32_le(re_buf_.data() + 8);
    uint32_t sender_kbps = read_u32_le(re_buf_.data() + 12);

    if (w == 0 || h == 0 || w > 7680 || h > 4320) {
        return;
    }

    pending_data_.assign(re_buf_.data() + protocol::kVideoHeaderSize, re_buf_.data() + re_buf_.size());
    pending_kbps_ = sender_kbps;
    pending_ts_ = sender_ts;
    has_pending_ = true;
}

void ScreenReceiver::push_audio_packet(const uint8_t* data, size_t len) {
    if (!opus_decoder_) {
        return;
    }

    if (len <= protocol::kAudioHeaderSize) {
        return;
    }

    uint16_t seq = read_u16_le(data);
    uint32_t sender_ts = read_u32_le(data + 2);
    const uint8_t* opus_data = data + protocol::kAudioHeaderSize;
    int opus_len = static_cast<int>(len - protocol::kAudioHeaderSize);

    int samples = opus_decoder_->decode(opus_data, opus_len, audio_decode_buf_.data(), opus::kFrameSize);

    if (samples <= 0) {
        return;
    }

    for (int i = 0; i < samples; ++i) {
        float l = audio_decode_buf_[static_cast<size_t>(i) * 2];
        float r = audio_decode_buf_[static_cast<size_t>(i) * 2 + 1];
        audio_mono_buf_[static_cast<size_t>(i)] = (l + r) * 0.5f;
    }

    jitter_.push_audio(audio_mono_buf_.data(), static_cast<size_t>(samples), seq, sender_ts);

    if (seq % 50 == 0) {
        LOG_INFO()
            << "[screen-audio-recv] seq=" << seq << " sender_ts=" << sender_ts << " samples=" << samples
            << " buffered=" << jitter_.audio_buffered_ms() << "ms";
    }
}

const ScreenStreamJitter::VideoFrame* ScreenReceiver::update() {
    std::vector<uint8_t> data;
    uint32_t kbps = 0, ts = 0;
    bool has_data = false;

    {
        std::scoped_lock lk(mutex_);
        if (has_pending_) {
            data.swap(pending_data_);
            kbps = pending_kbps_;
            ts = pending_ts_;
            has_pending_ = false;
            has_data = true;
        }
    }

    if (has_data) {
        if (!decoder_inited_) {
            decoder_inited_ = decoder_.init();
            if (!decoder_inited_) {
                LOG_ERROR() << "failed to init video decoder";
            }
        }

        if (decoder_inited_) {
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            if (decoder_.decode(data.data(), data.size(), rgba, w, h)) {
                decode_failures_ = 0;
                width_ = w;
                height_ = h;
                measured_kbps_ = static_cast<int>(kbps);
                jitter_.push_video(std::move(rgba), w, h, ts);
            } else {
                ++decode_failures_;
                auto now = std::chrono::steady_clock::now();
                auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_keyframe_req_).count();
                if (on_keyframe_needed_ && (decode_failures_ == 1 || since >= 500)) {
                    on_keyframe_needed_();
                    last_keyframe_req_ = now;
                }
            }
        }
    }

    return jitter_.pop_video();
}

std::string ScreenReceiver::active_peer() const {
    std::scoped_lock lk(mutex_);
    return current_peer_;
}

bool ScreenReceiver::active() const {
    std::scoped_lock lk(mutex_);
    if (current_peer_.empty()) {
        return false;
    }
    auto age = std::chrono::steady_clock::now() - last_packet_;
    return std::chrono::duration_cast<std::chrono::seconds>(age).count() <= kStaleSeconds;
}

void ScreenReceiver::reset() {
    {
        std::scoped_lock lk(mutex_);
        current_peer_.clear();
        re_frame_id_ = 0;
        re_total_ = 0;
        re_got_ = 0;
        re_buf_.clear();
        pending_data_.clear();
        has_pending_ = false;
    }
    jitter_.reset();
    decode_failures_ = 0;
    width_ = 0;
    height_ = 0;
    measured_kbps_ = 0;
}
