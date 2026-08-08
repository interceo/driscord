#include "video_transport.hpp"

#include "channel_labels.hpp"
#include "config.hpp"
#include "log.hpp"
#include "utils/protocol.hpp"

#include <cstring>

namespace {

constexpr uint8_t kKeyframeRequestTag = 0x01;
constexpr uint8_t kStopStreamTag = 0x02;

} // namespace

VideoTransport::VideoTransport(Transport& transport)
    : transport_(transport)
{
    // Video: pure data, no embedded control tags.
    transport.register_channel({
        .label = channel::MediaChannel::Video,
        .unordered = true,
        .max_retransmits = 0,
        .on_data =
            [this](const std::string& peer_id, const uint8_t* data, size_t len) {
                on_frame(peer_id, data, len);
            },
        .on_open =
            [this] {
                // The media path just came up; whatever we were showing is
                // stale, so ask the current streamer for a fresh keyframe.
                std::scoped_lock lk(sink_mutex_);
                if (on_keyframe_needed_) {
                    on_keyframe_needed_();
                }
            },
        .on_close = nullptr,
    });

    // Control: ordered, reliable — keyframe requests and stream lifecycle.
    // Identity is exchanged at the signaling layer (Transport::peer_username).
    transport.register_channel({
        .label = channel::MediaChannel::Control,
        .unordered = false,
        .max_retransmits = -1, // reliable
        .on_data =
            [this](const std::string& peer_id, const uint8_t* data, size_t len) {
                on_control(peer_id, data, len);
            },
        .on_open = nullptr,
        .on_close = nullptr,
    });
}

void VideoTransport::send_video(const uint8_t* data, size_t len)
{
    if (len == 0) {
        return;
    }

    // Backpressure: if the send buffer is already deep, the uplink cannot keep
    // up. Queueing this frame would only add latency, so drop it and ask the
    // encoder for a keyframe to resync once the buffer drains.
    if (transport_.channel_buffered_amount(channel::MediaChannel::Video)
        > stream_defaults::kVideoSendBufferLimitBytes) {
        const uint64_t n = ++frames_dropped_backpressure_;
        if (n == 1 || n % 60 == 0) {
            LOG_WARNING() << "[video] backpressure, dropped frame #" << n;
        }
        return;
    }

    const uint64_t frame_id = next_frame_id_++;
    const size_t wire_len = protocol::FrameHeader::kWireSize + len;

    // One message per frame — SCTP fragments it. Unordered with no
    // retransmits, so a lost fragment costs this frame and nothing more.
    rtc::binary pkt(wire_len);
    protocol::FrameHeader { frame_id }.serialize(
        reinterpret_cast<uint8_t*>(pkt.data()));
    std::memcpy(pkt.data() + protocol::FrameHeader::kWireSize, data, len);

    transport_.send_on_channel(channel::MediaChannel::Video, std::move(pkt));
}

void VideoTransport::send_keyframe_request()
{
    transport_.send_on_channel(channel::MediaChannel::Control, &kKeyframeRequestTag, 1);
}

void VideoTransport::send_stop_stream()
{
    transport_.send_on_channel(channel::MediaChannel::Control, &kStopStreamTag, 1);
}

void VideoTransport::on_new_streaming_peer(
    std::function<void(const std::string&)> cb)
{
    std::scoped_lock lk(streaming_mutex_);
    on_new_streaming_peer_ = std::move(cb);
}

void VideoTransport::on_streaming_peer_removed(
    std::function<void(const std::string&)> cb)
{
    std::scoped_lock lk(streaming_mutex_);
    on_streaming_peer_removed_ = std::move(cb);
}

void VideoTransport::remove_streaming_peer(const std::string& peer_id)
{
    bool was_present;
    std::function<void(const std::string&)> cb;
    {
        std::scoped_lock lk(streaming_mutex_);
        was_present = seen_streaming_.erase(peer_id) > 0;
        cb = on_streaming_peer_removed_;
    }
    if (was_present && cb) {
        cb(peer_id);
    }
}

void VideoTransport::add_watched_peer(const std::string& peer_id)
{
    std::scoped_lock lk(sink_mutex_);
    watched_peers_.insert(peer_id);
}

void VideoTransport::remove_watched_peer(const std::string& peer_id)
{
    std::scoped_lock lk(sink_mutex_);
    watched_peers_.erase(peer_id);
}

void VideoTransport::clear_watched_peers()
{
    std::scoped_lock lk(sink_mutex_);
    watched_peers_.clear();
}

bool VideoTransport::watching() const
{
    std::scoped_lock lk(sink_mutex_);
    return !watched_peers_.empty();
}

void VideoTransport::set_video_sink(VideoPacketCb video_cb, KeyframeCb kf_cb)
{
    std::scoped_lock lk(sink_mutex_);
    on_video_sink_ = std::move(video_cb);
    on_keyframe_needed_ = std::move(kf_cb);
}

void VideoTransport::clear_video_sink()
{
    std::scoped_lock lk(sink_mutex_);
    on_video_sink_ = nullptr;
    on_keyframe_needed_ = nullptr;
}

void VideoTransport::on_assembled(const std::string& peer_id,
    const uint8_t* data,
    size_t len,
    uint64_t frame_id)
{
    {
        std::function<void(const std::string&)> cb;
        bool is_new;
        {
            std::scoped_lock lk(streaming_mutex_);
            is_new = seen_streaming_.insert(peer_id).second;
            cb = on_new_streaming_peer_;
        }
        if (is_new && cb) {
            cb(peer_id);
        }
    }
    {
        std::scoped_lock lk(sink_mutex_);
        if (on_video_sink_ && watched_peers_.count(peer_id) > 0) {
            on_video_sink_(peer_id, data, len, frame_id);
        }
    }
}

void VideoTransport::on_frame(const std::string& peer_id,
    const uint8_t* data,
    size_t len)
{
    const auto header = protocol::FrameHeader::deserialize({ data, len });
    if (!header) {
        return;
    }
    const size_t payload_len = len - protocol::FrameHeader::kWireSize;
    if (payload_len == 0) {
        return;
    }
    if (payload_len > kMaxFrameBytes) {
        LOG_WARNING() << "[video] oversized frame from " << peer_id << " ("
                      << payload_len << " bytes), dropping";
        return;
    }
    on_assembled(peer_id, data + protocol::FrameHeader::kWireSize, payload_len,
        header->frame_id);
}

void VideoTransport::on_control(const std::string& peer_id,
    const uint8_t* data,
    size_t len)
{
    if (len == 0) {
        return;
    }
    if (data[0] == kKeyframeRequestTag) {
        std::scoped_lock lk(sink_mutex_);
        if (on_keyframe_needed_) {
            on_keyframe_needed_();
        }
    } else if (data[0] == kStopStreamTag) {
        remove_streaming_peer(peer_id);
    }
}
