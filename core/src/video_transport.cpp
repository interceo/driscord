#include "video_transport.hpp"

#include "channel_labels.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstring>

namespace {

constexpr uint8_t kKeyframeRequestTag = 0x01;
constexpr uint8_t kStopStreamTag = 0x02;

} // namespace

VideoTransport::VideoTransport(Transport& transport)
    : transport_(transport)
{
    // Video channel: pure data, no embedded control tags.
    transport.register_channel({
        .label = channel::kVideo,
        .unordered = true,
        .max_retransmits = 0,
        .on_data =
            [this](const std::string& peer_id, const uint8_t* data, size_t len) {
                on_chunk(peer_id, data, len);
            },
        .on_open =
            [this](const std::string& peer_id) {
                // Reset stale streaming state from any previous connection to this
                // peer. remove_streaming_peer is a no-op if the peer wasn't
                // streaming before.
                remove_streaming_peer(peer_id);
                std::scoped_lock lk(sink_mutex_);
                if (on_keyframe_needed_) {
                    on_keyframe_needed_();
                }
            },
        .on_close =
            [this](const std::string& peer_id) {
                remove_streaming_peer(peer_id);
                std::scoped_lock lk(streaming_mutex_);
                video_subscribers_.erase(peer_id);
            },
    });

    // Control channel: ordered, reliable — carries keyframe requests and stream
    // lifecycle signals. Separate from video data to eliminate the len==1 ambiguity.
    transport.register_channel({
        .label = channel::kControl,
        .unordered = false,
        .max_retransmits = -1, // reliable
        .on_data =
            [this](const std::string& peer_id, const uint8_t* data, size_t len) {
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

    std::vector<std::string> subscribers;
    {
        std::scoped_lock lk(streaming_mutex_);
        if (video_subscribers_.empty()) {
            return;
        }
        subscribers.assign(video_subscribers_.begin(), video_subscribers_.end());
    }

    // 4.6: collect all open DCs in one lock acquisition instead of N×M.
    auto dcs = transport_.get_open_channels(channel::kVideo, subscribers);
    if (dcs.empty()) {
        return;
    }

    const uint64_t frame_id = next_frame_id_++;
    const auto total = static_cast<uint16_t>((len + kChunkPayloadSize - 1) / kChunkPayloadSize);

    for (uint16_t i = 0; i < total; ++i) {
        const size_t offset = static_cast<size_t>(i) * kChunkPayloadSize;
        const size_t chunk_len = std::min(kChunkPayloadSize, len - offset);
        const size_t wire_len = protocol::ChunkHeader::kWireSize + chunk_len;

        rtc::binary pkt(wire_len);
        protocol::ChunkHeader { frame_id, i, total }.serialize(
            reinterpret_cast<uint8_t*>(pkt.data()));
        std::memcpy(pkt.data() + protocol::ChunkHeader::kWireSize, data + offset, chunk_len);

        // Copy to all but last, move to last (avoids internal copy in libdatachannel).
        for (size_t s = 0; s + 1 < dcs.size(); ++s) {
            try {
                dcs[s]->send(reinterpret_cast<const std::byte*>(pkt.data()), wire_len);
            } catch (const std::exception& e) {
                LOG_ERROR() << "send_video chunk[" << i << "] to dc[" << s << "]: " << e.what();
            }
        }
        try {
            dcs.back()->send(std::move(pkt));
        } catch (const std::exception& e) {
            LOG_ERROR() << "send_video chunk[" << i << "] to last dc: " << e.what();
        }
    }
}

void VideoTransport::send_keyframe_request()
{
    transport_.send_on_channel(channel::kControl, &kKeyframeRequestTag, 1);
}

void VideoTransport::send_stop_stream()
{
    transport_.send_on_channel(channel::kControl, &kStopStreamTag, 1);
}

void VideoTransport::add_subscriber(const std::string& peer_id)
{
    {
        std::scoped_lock lk(streaming_mutex_);
        video_subscribers_.insert(peer_id);
    }
    std::scoped_lock lk(sink_mutex_);
    if (on_keyframe_needed_) {
        on_keyframe_needed_();
    }
}

void VideoTransport::remove_subscriber(const std::string& peer_id)
{
    std::scoped_lock lk(streaming_mutex_);
    video_subscribers_.erase(peer_id);
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
    peer_assembly_.erase(peer_id);
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

void VideoTransport::on_chunk(const std::string& peer_id,
    const uint8_t* data,
    size_t len)
{
    auto [it, _] = peer_assembly_.try_emplace(peer_id, kChunkPayloadSize, 8,
        kMaxChunksPerFrame);
    it->second.push(
        data, len,
        [&](uint64_t frame_id, const uint8_t* frame_data, size_t frame_len) {
            on_assembled(peer_id, frame_data, frame_len, frame_id);
        });
}
