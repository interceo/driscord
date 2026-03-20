#include "video.hpp"

#include "log.hpp"

#include <cstring>

namespace {

using namespace utils;

constexpr int kStaleSeconds = 3;

} // namespace

VideoSender::VideoSender() = default;
VideoSender::~VideoSender() {
    stop();
}

bool VideoSender::start(const size_t fps, const size_t base_bitrate_kbps, SendCb on_video) {
    if (sharing_) {
        return false;
    }

    fps_               = fps;
    base_bitrate_kbps_ = base_bitrate_kbps;
    on_video_          = std::move(on_video);

    encode_running_ = true;
    encode_thread_  = std::thread(&VideoSender::encode_loop, this);
    sharing_        = true;

    LOG_INFO() << "video sender started fps=" << fps << " bitrate=" << base_bitrate_kbps << " kbps";
    return true;
}

void VideoSender::stop() {
    if (!sharing_) {
        return;
    }

    encode_running_ = false;
    frame_cv_.notify_one();
    if (encode_thread_.joinable()) {
        encode_thread_.join();
    }

    video_encoder_.shutdown();
    sharing_  = false;
    on_video_ = nullptr;

    LOG_INFO() << "video sender stopped";
}

void VideoSender::push_frame(ScreenCapture::Frame&& frame) {
    if (!sharing_) {
        return;
    }

    frame.capture_ts = utils::WallNow();
    {
        std::scoped_lock lk(frame_mutex_);
        pending_frame_ = std::move(frame);
        frame_ready_   = true;
    }
    frame_cv_.notify_one();
}

void VideoSender::encode_loop() {
    while (encode_running_) {
        ScreenCapture::Frame frame;
        {
            std::unique_lock lk(frame_mutex_);
            frame_cv_.wait_for(lk, std::chrono::milliseconds(100), [this] {
                return frame_ready_ || !encode_running_;
            });
            if (!frame_ready_) {
                continue;
            }
            frame        = std::move(pending_frame_);
            frame_ready_ = false;
        }

        if (frame.data.empty() || !encode_running_) {
            continue;
        }

        if (frame.width != video_encoder_.width() || frame.height != video_encoder_.height()) {
            if (!video_encoder_.init(frame.width, frame.height, fps_, base_bitrate_kbps_)) {
                continue;
            }
        }

        const auto& encoded = video_encoder_.encode(frame.data, frame.width, frame.height);
        if (encoded.empty()) {
            continue;
        }

        const auto capture_ts =
            frame.capture_ts.time_since_epoch().count() != 0 ? frame.capture_ts : WallNow();

        const protocol::VideoHeader vh{
            .width             = static_cast<uint32_t>(frame.width),
            .height            = static_cast<uint32_t>(frame.height),
            .sender_ts         = capture_ts,
            .bitrate_kbps      = static_cast<uint32_t>(video_encoder_.measured_kbps()),
            .frame_duration_us = static_cast<uint32_t>(1'000'000 / fps_),
        };
        frame_buf_.resize(protocol::VideoHeader::kWireSize + encoded.size());
        vh.serialize(frame_buf_.data());
        std::memcpy(
            frame_buf_.data() + protocol::VideoHeader::kWireSize,
            encoded.data(),
            encoded.size()
        );

        on_video_(frame_buf_.data(), frame_buf_.size());
    }
}

VideoReceiver::VideoReceiver(int buffer_ms, int /*max_sync_gap_ms*/)
    : buffer_delay_(std::chrono::milliseconds(buffer_ms)) {}

VideoReceiver::~VideoReceiver() = default;

void VideoReceiver::set_keyframe_callback(std::function<void()> fn) {
    on_keyframe_needed_ = std::move(fn);
}

void VideoReceiver::push_video_packet(
    const std::string& peer_id,
    const utils::vector_view<const uint8_t> data
) {
    if (data.size() <= protocol::VideoHeader::kWireSize) {
        return;
    }

    const auto vh = protocol::VideoHeader::deserialize(data.data());
    if (vh.width == 0 || vh.height == 0 || vh.width > 7680 || vh.height > 4320) {
        return;
    }

    // Find or create per-peer decoder. Each peer has its own H.264 codec context
    // so simultaneous streams don't corrupt each other's decoder state.
    // Use shared_ptr so reset() clearing the map doesn't leave a dangling pointer.
    std::shared_ptr<PeerDecoder> ps;
    {
        std::scoped_lock lk(mutex_);
        current_peer_ = peer_id;
        last_packet_  = utils::Now();

        auto& entry = peer_decoders_[peer_id];
        if (!entry) {
            entry = std::make_shared<PeerDecoder>(buffer_delay_);
            if (!entry->decoder.init()) {
                LOG_ERROR() << "video decoder init failed for peer " << peer_id;
                peer_decoders_.erase(peer_id);
                return;
            }
        }
        ps = entry;
    }

    if (!ps || !ps->decoder.ready()) {
        return;
    }

    bytes_since_calc_ += data.size();
    const auto now        = utils::Now();
    const auto elapsed_ms = utils::ElapsedMs(last_calc_, now);

    if (elapsed_ms >= 1000) {
        measured_kbps_
            .store(static_cast<int>(bytes_since_calc_ * 8 / elapsed_ms), std::memory_order_relaxed);
        bytes_since_calc_ = 0;
        last_calc_        = now;
    }

    const uint8_t* encoded   = data.data() + protocol::VideoHeader::kWireSize;
    const size_t encoded_len = data.size() - protocol::VideoHeader::kWireSize;

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (ps->decoder.decode(encoded, encoded_len, rgba, w, h)) {
        ps->decode_failures = 0;
        ps->jitter.push(
            Frame{
                .rgba           = std::move(rgba),
                .width          = w,
                .height         = h,
                .peer_id        = peer_id,
                .sender_ts      = vh.sender_ts,
                .frame_duration = std::chrono::microseconds(vh.frame_duration_us),
            }
        );
    } else {
        ++ps->decode_failures;
        const auto kf_now = utils::Now();
        if (on_keyframe_needed_ &&
            (ps->decode_failures == 1 || utils::ElapsedMs(ps->last_keyframe_req, kf_now) >= 500)) {
            on_keyframe_needed_();
            ps->last_keyframe_req = kf_now;
        }
    }
}

const VideoReceiver::Frame* VideoReceiver::update() {
    std::vector<std::shared_ptr<PeerDecoder>> peers;
    {
        std::scoped_lock lk(mutex_);
        for (auto& [_, pd] : peer_decoders_) {
            peers.push_back(pd);
        }
    }
    for (auto& pd : peers) {
        while (true) {
            auto frame = pd->jitter.pop();
            if (!frame || frame->rgba.empty()) {
                break;
            }
            current_frame_ = std::move(frame);
        }
    }
    return current_frame_.get();
}

VideoReceiver::Stats VideoReceiver::video_stats() const {
    std::shared_ptr<PeerDecoder> pd;
    {
        std::scoped_lock lk(mutex_);
        auto it = peer_decoders_.find(current_peer_);
        if (it != peer_decoders_.end()) {
            pd = it->second;
        }
    }
    return pd ? pd->jitter.stats() : Stats{};
}

size_t VideoReceiver::evict_old(utils::Duration max_delay) {
    std::vector<std::shared_ptr<PeerDecoder>> peers;
    {
        std::scoped_lock lk(mutex_);
        for (auto& [_, p] : peer_decoders_) {
            peers.push_back(p);
        }
    }
    size_t total = 0;
    for (auto& p : peers) {
        total += p->jitter.evict_old(max_delay);
    }
    return total;
}

size_t VideoReceiver::evict_before_sender_ts(utils::WallTimestamp cutoff) {
    std::vector<std::shared_ptr<PeerDecoder>> peers;
    {
        std::scoped_lock lk(mutex_);
        for (auto& [_, p] : peer_decoders_) {
            peers.push_back(p);
        }
    }
    size_t total = 0;
    for (auto& p : peers) {
        total += p->jitter.evict_before_sender_ts(cutoff);
    }
    return total;
}

std::optional<utils::WallTimestamp> VideoReceiver::front_effective_ts() const {
    std::shared_ptr<PeerDecoder> pd;
    {
        std::scoped_lock lk(mutex_);
        auto it = peer_decoders_.find(current_peer_);
        if (it != peer_decoders_.end()) {
            pd = it->second;
        }
    }
    return pd ? pd->jitter.front_effective_ts() : std::nullopt;
}

utils::Duration VideoReceiver::front_frame_duration() const {
    std::shared_ptr<PeerDecoder> pd;
    {
        std::scoped_lock lk(mutex_);
        auto it = peer_decoders_.find(current_peer_);
        if (it != peer_decoders_.end()) {
            pd = it->second;
        }
    }
    if (!pd) {
        return {};
    }
    return pd->jitter.with_front([](const Frame& f) { return f.frame_duration; })
        .value_or(utils::Duration{});
}

bool VideoReceiver::primed() const {
    std::shared_ptr<PeerDecoder> pd;
    {
        std::scoped_lock lk(mutex_);
        auto it = peer_decoders_.find(current_peer_);
        if (it != peer_decoders_.end()) {
            pd = it->second;
        }
    }
    return pd && pd->jitter.primed();
}

int64_t VideoReceiver::front_age_ms() const {
    std::shared_ptr<PeerDecoder> pd;
    {
        std::scoped_lock lk(mutex_);
        auto it = peer_decoders_.find(current_peer_);
        if (it != peer_decoders_.end()) {
            pd = it->second;
        }
    }
    return pd ? pd->jitter.front_age_ms() : -1;
}

std::string VideoReceiver::active_peer() const {
    std::scoped_lock lk(mutex_);
    return current_peer_;
}

bool VideoReceiver::active() const {
    std::scoped_lock lk(mutex_);
    if (current_peer_.empty()) {
        return false;
    }
    return utils::ElapsedMs(last_packet_) / 1000 <= kStaleSeconds;
}

void VideoReceiver::reset() {
    {
        std::scoped_lock lk(mutex_);
        current_peer_.clear();
        last_packet_ = {};
        peer_decoders_.clear();
    }
    current_frame_.reset();
    measured_kbps_.store(0, std::memory_order_relaxed);
    bytes_since_calc_ = 0;
    last_calc_        = utils::Now();
}
