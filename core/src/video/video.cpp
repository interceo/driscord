#include "video.hpp"

#include "log.hpp"
#include "utils/enum_strings.hpp"
#include "utils/mono_clock.hpp"
#include "utils/protocol.hpp"

#include <cstring>
#include <optional>

namespace {

using namespace utils;

constexpr int kStaleSeconds = 3;

// A peer that has stopped sending for this long is treated as gone.
constexpr int64_t kBitrateWindowMs = 1000;
// Don't ask for a keyframe more often than this while a stream is broken —
// each request costs the sender a full intra frame.
constexpr int64_t kKeyframeRetryMs = 500;
// Beyond 8K the header is more likely corrupt than honest.
constexpr uint32_t kMaxWidth = 7680;
constexpr uint32_t kMaxHeight = 4320;

} // namespace

VideoSender::VideoSender() = default;
VideoSender::~VideoSender()
{
    stop();
}

bool VideoSender::start(const size_t fps,
    const size_t base_bitrate_kbps,
    SendCb on_video)
{
    if (sharing_) {
        return false;
    }

    fps_ = fps;
    base_bitrate_kbps_ = base_bitrate_kbps;
    on_video_ = std::move(on_video);

    encode_running_ = true;
    encode_thread_ = std::thread(&VideoSender::encode_loop, this);
    sharing_ = true;

    LOG_INFO() << "video sender started fps=" << fps
               << " bitrate=" << base_bitrate_kbps << " kbps";
    return true;
}

void VideoSender::stop()
{
    if (!sharing_) {
        return;
    }

    encode_running_ = false;
    frame_cv_.notify_one();
    if (encode_thread_.joinable()) {
        encode_thread_.join();
    }

    video_encoder_.shutdown();
    sharing_ = false;
    on_video_ = nullptr;

    LOG_INFO() << "video sender stopped";
}

void VideoSender::push_frame(ScreenCapture::Frame& frame)
{
    if (!sharing_) {
        return;
    }

    frame.capture_ts_us = utils::MonoClock::now_us();
    {
        std::scoped_lock lk(frame_mutex_);
        // Swap rather than move: the caller gets whatever buffer was sitting
        // here and refills it, so a steady capture allocates nothing after the
        // first few frames. Only the newest frame is kept — the encoder is
        // slower than the capture, and a stale frame is worth nothing.
        std::swap(pending_frame_, frame);
        frame_ready_ = true;
    }
    frame_cv_.notify_one();
}

void VideoSender::encode_loop()
{
    while (encode_running_) {
        ScreenCapture::Frame frame;
        {
            std::unique_lock lk(frame_mutex_);
            frame_cv_.wait_for(lk, std::chrono::milliseconds(100),
                [this] { return frame_ready_ || !encode_running_; });
            if (!frame_ready_) {
                continue;
            }
            frame = std::move(pending_frame_);
            frame_ready_ = false;
        }

        if (frame.data.empty() || !encode_running_) {
            continue;
        }

        if (frame.width != video_encoder_.width() || frame.height != video_encoder_.height()) {
            if (auto r = video_encoder_.init(frame.width, frame.height, fps_,
                    base_bitrate_kbps_);
                !r) {
                LOG_ERROR() << "video encoder reinit failed: " << utils::to_string(r.error());
                continue;
            }
        }

        const auto& encoded = video_encoder_.encode(frame.data, frame.width, frame.height);
        if (encoded.empty()) {
            continue;
        }

        const auto capture_ts_us = frame.capture_ts_us != 0
            ? frame.capture_ts_us
            : utils::MonoClock::now_us();

        const protocol::VideoHeader vh {
            .width = static_cast<uint32_t>(frame.width),
            .height = static_cast<uint32_t>(frame.height),
            .sender_ts_us = capture_ts_us,
            .bitrate_kbps = static_cast<uint32_t>(video_encoder_.measured_kbps()),
            .frame_duration_us = static_cast<uint32_t>(1'000'000 / fps_),
            .codec = video_encoder_.codec(),
        };
        frame_buf_.resize(protocol::VideoHeader::kWireSize + encoded.size());
        vh.serialize(frame_buf_.data());
        std::memcpy(frame_buf_.data() + protocol::VideoHeader::kWireSize,
            encoded.data(), encoded.size());

        on_video_(frame_buf_.data(), frame_buf_.size());
    }
}

VideoReceiver::VideoReceiver(std::string peer_id,
    std::shared_ptr<avsync::MediaClock> clock)
    : peer_id_(std::move(peer_id))
    , clock_(std::move(clock))
{
    // Decoder is lazy-initialised on the first packet so we know the codec.
}

VideoReceiver::~VideoReceiver() = default;

void VideoReceiver::set_keyframe_callback(std::function<void()> fn)
{
    on_keyframe_needed_ = std::move(fn);
}

void VideoReceiver::push_video_packet(
    const utils::vector_view<const uint8_t> data,
    uint64_t frame_id)
{
    if (data.size() <= protocol::VideoHeader::kWireSize) {
        return;
    }

    const auto vh = protocol::VideoHeader::deserialize(data.data());
    if (vh.width == 0 || vh.height == 0 || vh.width > kMaxWidth || vh.height > kMaxHeight) {
        return;
    }

    if (!decoder_codec_.has_value() || *decoder_codec_ != vh.codec) {
        LOG_INFO() << "video decoder: init " << utils::to_string(vh.codec)
                   << " for peer " << peer_id_;
        if (!decoder_.init(vh.codec)) {
            LOG_ERROR() << "video decoder init failed for peer " << peer_id_;
            return;
        }
        decoder_codec_ = vh.codec;
        std::scoped_lock lk(mutex_);
        buffer_.reset();
    }

    packets_received_.inc();

    // The frame is complete only now, after every chunk of it has been
    // reassembled — which is exactly the delay that makes video lag audio, and
    // exactly what the shared clock has to measure.
    clock_->observe(avsync::MediaClock::Stream::Video, vh.sender_ts_us,
        utils::MonoClock::now_us());

    const auto now = utils::Now();
    {
        std::scoped_lock lk(mutex_);
        last_packet_ = now;
    }

    bytes_since_calc_ += data.size();
    const auto elapsed_ms = utils::ElapsedMs(last_calc_, now);
    if (elapsed_ms >= kBitrateWindowMs) {
        measured_kbps_.store(static_cast<int>(bytes_since_calc_ * 8 / elapsed_ms),
            std::memory_order_relaxed);
        bytes_since_calc_ = 0;
        last_calc_ = now;
    }

    const uint8_t* encoded = data.data() + protocol::VideoHeader::kWireSize;
    const size_t encoded_len = data.size() - protocol::VideoHeader::kWireSize;

    // Decode into a recycled buffer. A 1080p frame is 8 MB, so allocating one
    // per frame — sixty times a second — dominates everything else this class
    // does. Buffers come back here as frames are shown or superseded.
    std::vector<uint8_t> rgba = take_spare();
    int w = 0, h = 0;
    if (!decoder_.decode(encoded, encoded_len, rgba, w, h)) {
        recycle(std::move(rgba));
        ++decode_failures_;
        total_decode_failures_.inc();
        const auto kf_now = utils::Now();
        if (on_keyframe_needed_
            && (decode_failures_ == 1
                || utils::ElapsedMs(last_keyframe_req_, kf_now) >= kKeyframeRetryMs)) {
            keyframe_requests_.inc();
            on_keyframe_needed_();
            last_keyframe_req_ = kf_now;
        }
        return;
    }

    decode_failures_ = 0;

    std::scoped_lock lk(mutex_);
    Frame frame {
        .rgba = std::move(rgba),
        .width = w,
        .height = h,
        .peer_id = peer_id_,
        .sender_ts_us = vh.sender_ts_us,
    };
    if (buffer_.push(frame_id, std::move(frame)) != utils::PushResult::Stored) {
        drop_count_.inc();
        recycle_locked(std::move(frame.rgba));
    }
}

std::vector<uint8_t> VideoReceiver::take_spare()
{
    std::scoped_lock lk(mutex_);
    if (spare_.empty()) {
        return { };
    }
    std::vector<uint8_t> buf = std::move(spare_.back());
    spare_.pop_back();
    return buf;
}

void VideoReceiver::recycle(std::vector<uint8_t>&& buf)
{
    std::scoped_lock lk(mutex_);
    recycle_locked(std::move(buf));
}

void VideoReceiver::recycle_locked(std::vector<uint8_t>&& buf)
{
    // A couple of spares is all the pipeline can have in flight at once;
    // holding more would just pin memory after a resolution change.
    if (buf.capacity() > 0 && spare_.size() < kMaxSpareBuffers) {
        spare_.push_back(std::move(buf));
    }
}

void VideoReceiver::update(const std::function<void(const Frame&)>& on_frame)
{
    const int64_t now = utils::MonoClock::now_us();
    const bool clock_ready = clock_->ready();

    {
        std::scoped_lock lk(mutex_);

        // Walk forward over everything already due and keep only the last one.
        // Anything before it would be shown and immediately replaced, so it is
        // dropped rather than drawn.
        std::optional<uint64_t> newest_due;
        for (auto seq = buffer_.next_present(); seq;
            seq = buffer_.next_present(*seq + 1)) {
            const Frame* f = buffer_.peek(*seq);
            if (clock_ready && clock_->deadline_us(f->sender_ts_us) > now) {
                break;
            }
            if (newest_due) {
                late_count_.inc();
            }
            newest_due = seq;
        }

        if (newest_due) {
            // Everything skipped over, plus the frame being replaced, gives up
            // its buffer here rather than being freed.
            for (auto seq = buffer_.next_present(); seq && *seq < *newest_due;
                seq = buffer_.next_present(*seq + 1)) {
                recycle_locked(std::move(buffer_.take(*seq).rgba));
            }
            recycle_locked(std::move(current_frame_.rgba));
            current_frame_ = buffer_.take(*newest_due);
            buffer_.advance_base_to(*newest_due + 1);
        }
    }

    if (!current_frame_.empty()) {
        on_frame(current_frame_);
    }
}

VideoReceiver::Stats VideoReceiver::video_stats() const
{
    size_t queued = 0;
    {
        std::scoped_lock lk(mutex_);
        queued = buffer_.size();
    }
    return {
        .queue_size = queued,
        .drop_count = drop_count_.load(),
        .late_count = late_count_.load(),
        .packets_received = packets_received_.load(),
        .decode_failures = total_decode_failures_.load(),
        .keyframe_requests = keyframe_requests_.load(),
        .measured_kbps = measured_kbps_.load(std::memory_order_relaxed),
        .target_delay_ms = clock_->target_delay_us() / 1000,
    };
}

bool VideoReceiver::active() const
{
    std::scoped_lock lk(mutex_);
    return utils::ElapsedMs(last_packet_) / 1000 <= kStaleSeconds;
}

void VideoReceiver::reset()
{
    {
        std::scoped_lock lk(mutex_);
        buffer_.reset();
        current_frame_ = Frame { };
        last_packet_ = utils::Timestamp { };
        decode_failures_ = 0;
        last_keyframe_req_ = utils::Timestamp { };
    }
    measured_kbps_.store(0, std::memory_order_relaxed);
    bytes_since_calc_ = 0;
    last_calc_ = utils::Now();
    packets_received_.reset();
    drop_count_.reset();
    late_count_.reset();
    total_decode_failures_.reset();
    keyframe_requests_.reset();
}
