#pragma once

#include "transport.hpp"
#include "utils/protocol.hpp"

#include <unordered_map>
#include <vector>

// Handles the video data channel: video frames + keyframe negotiation.
// The DataChannel runs unreliable+unordered (max_retransmits=0): lost chunks
// drop the whole frame; the receiver requests a keyframe and the decoder
// recovers from the next IDR. Frames are split into kChunkPayloadSize-byte
// chunks so each DataChannel message fits in one IP packet with no SCTP-level
// fragmentation (which would reintroduce HoL blocking even in unreliable mode).
// Must be constructed before Transport::connect() is called.
class VideoTransport {
public:
    using PacketCb = Transport::PacketCb;
    using Callback = std::function<void()>;

    // Keep well under PMTU (~1400 bytes after DTLS/SCTP/UDP/IP headers).
    static constexpr size_t kChunkPayloadSize = 1100;

    explicit VideoTransport(Transport& transport);

    void send_video(const uint8_t* data, size_t len);
    void send_keyframe_request();
    void send_stop_stream();

    void on_video_received(PacketCb cb)                              { on_video_       = std::move(cb); }
    void on_video_channel_opened(Callback cb)                        { on_opened_      = std::move(cb); }
    void on_keyframe_requested(Callback cb)                          { on_kf_req_      = std::move(cb); }
    void on_stream_stopped(std::function<void(const std::string&)> cb) { on_stop_stream_ = std::move(cb); }

private:
    void on_chunk(const std::string& peer_id, const uint8_t* data, size_t len);

    Transport& transport_;
    PacketCb   on_video_;
    Callback   on_opened_;
    Callback   on_kf_req_;
    std::function<void(const std::string&)> on_stop_stream_;

    // Send side
    uint64_t             next_frame_id_ = 0;
    std::vector<uint8_t> chunk_buf_;

    // Receive-side reassembly.
    // Accessed only from the single DataChannel receive thread — no locking needed.
    struct FrameAssembly {
        std::vector<uint8_t> buffer;    // flat: chunk_idx * kChunkPayloadSize
        std::vector<bool>    received;  // per-chunk dedup flag
        uint16_t total          = 0;
        uint16_t received_count = 0;
        size_t   actual_size    = 0;    // max(chunk_idx * stride + payload_len) across received chunks
    };
    std::unordered_map<uint64_t, FrameAssembly> assembly_;
    static constexpr size_t kMaxAssemblyFrames = 8;
};
