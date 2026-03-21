#pragma once

#include "transport.hpp"
#include "utils/protocol.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class VideoTransport {
public:
    using PacketCb    = Transport::PacketCb;
    using Callback    = std::function<void()>;
    using VideoPacketCb = PacketCb;
    using KeyframeCb    = Callback;

    // Keep well under PMTU (~1400 bytes after DTLS/SCTP/UDP/IP headers).
    static constexpr size_t   kChunkPayloadSize = 1100;
    // 512 chunks * 1100 bytes = ~560 KB max per frame; rejects malformed/malicious packets.
    static constexpr uint16_t kMaxChunksPerFrame = 512;

    explicit VideoTransport(Transport& transport);

    void send_video(const uint8_t* data, size_t len);
    void send_keyframe_request();
    void send_stop_stream();

    // Streaming peer lifecycle — fired when a peer starts/stops sending video.
    void on_new_streaming_peer(std::function<void(const std::string&)> cb);
    void on_streaming_peer_removed(std::function<void(const std::string&)> cb);
    void remove_streaming_peer(const std::string& peer_id);

    // Watching gate — only routes incoming video to the sink while true.
    void set_watching(bool w) { watching_.store(w, std::memory_order_relaxed); }
    bool watching() const { return watching_.load(std::memory_order_relaxed); }

    // Video sink — set by whichever component consumes incoming video.
    void set_video_sink(VideoPacketCb video_cb, KeyframeCb kf_cb);
    void clear_video_sink();

private:
    void on_chunk(const std::string& peer_id, const uint8_t* data, size_t len);
    void on_assembled(const std::string& peer_id, const uint8_t* data, size_t len);

    Transport& transport_;

    std::atomic<bool> watching_{false};

    std::mutex streaming_mutex_;
    std::set<std::string> seen_streaming_;
    std::function<void(const std::string&)> on_new_streaming_peer_;
    std::function<void(const std::string&)> on_streaming_peer_removed_;

    std::mutex sink_mutex_;
    VideoPacketCb on_video_sink_;
    KeyframeCb on_keyframe_needed_;

    uint64_t next_frame_id_ = 0;
    std::vector<uint8_t> chunk_buf_;

    struct FrameAssembly {
        std::vector<uint8_t> buffer;
        std::vector<bool> received;
        uint16_t total          = 0;
        uint16_t received_count = 0;
        size_t actual_size      = 0;
    };
    // Keyed by peer_id then frame_id to prevent frame_id collision across peers.
    std::unordered_map<std::string, std::unordered_map<uint64_t, FrameAssembly>> peer_assembly_;
    static constexpr size_t kMaxAssemblyFrames = 8;
};
