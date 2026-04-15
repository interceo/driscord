#pragma once

#include "transport.hpp"
#include "utils/chunk_assembler.hpp"
#include "utils/protocol.hpp"

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class VideoTransport {
public:
    using PacketCb = Transport::PacketCb;
    using Callback = std::function<void()>;
    using VideoPacketCb = std::function<void(const std::string&, const uint8_t*, size_t, uint64_t)>;
    using KeyframeCb = Callback;

    // Keep well under PMTU (~1400 bytes after DTLS/SCTP/UDP/IP headers).
    static constexpr size_t kChunkPayloadSize = 1100;
    // 512 chunks * 1100 bytes = ~560 KB max per frame; rejects
    // malformed/malicious packets.
    static constexpr uint16_t kMaxChunksPerFrame = 512;

    explicit VideoTransport(Transport& transport);

    void send_video(const uint8_t* data, size_t len);
    void send_keyframe_request();
    void send_stop_stream();
    void add_subscriber(const std::string& peer_id);
    void remove_subscriber(const std::string& peer_id);

    // Streaming peer lifecycle — fired when a peer starts/stops sending video.
    void on_new_streaming_peer(std::function<void(const std::string&)> cb);
    void on_streaming_peer_removed(std::function<void(const std::string&)> cb);
    void remove_streaming_peer(const std::string& peer_id);

    // Watching gate — only routes incoming video from explicitly added peers to
    // the sink.
    void add_watched_peer(const std::string& peer_id);
    void remove_watched_peer(const std::string& peer_id);
    void clear_watched_peers();
    bool watching() const;

    // Video sink — set by whichever component consumes incoming video.
    void set_video_sink(VideoPacketCb video_cb, KeyframeCb kf_cb);
    void clear_video_sink();

    // Identity exchange: broadcast our username to new peers on control-channel open.
    void set_local_username(const std::string& username);
    std::string peer_username(const std::string& peer_id) const;
    void on_peer_identity(std::function<void(const std::string&, const std::string&)> cb);

private:
    void on_chunk(const std::string& peer_id, const uint8_t* data, size_t len);
    void on_assembled(const std::string& peer_id,
        const uint8_t* data,
        size_t len,
        uint64_t frame_id);

    Transport& transport_;

    std::unordered_set<std::string> watched_peers_; // guarded by sink_mutex_

    std::mutex streaming_mutex_;
    std::set<std::string> seen_streaming_;
    std::unordered_set<std::string> video_subscribers_;
    std::function<void(const std::string&)> on_new_streaming_peer_;
    std::function<void(const std::string&)> on_streaming_peer_removed_;

    mutable std::mutex sink_mutex_;
    VideoPacketCb on_video_sink_;
    KeyframeCb on_keyframe_needed_;

    uint64_t next_frame_id_ = 0;

    // Keyed by peer_id to prevent frame_id collision across peers.
    std::unordered_map<std::string, utils::ChunkAssembler> peer_assembly_;

    // Identity exchange
    mutable std::mutex identity_mutex_;
    std::string local_username_;
    std::unordered_map<std::string, std::string> peer_usernames_;
    std::function<void(const std::string&, const std::string&)> on_peer_identity_;
};
