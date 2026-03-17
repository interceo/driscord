#pragma once

#include "transport.hpp"
#include "utils/protocol.hpp"

#include <mutex>
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

    void on_video_received(PacketCb cb)       { on_video_   = std::move(cb); }
    void on_video_channel_opened(Callback cb) { on_opened_  = std::move(cb); }
    void on_keyframe_requested(Callback cb)   { on_kf_req_  = std::move(cb); }

private:
    void on_chunk(const std::string& peer_id, const uint8_t* data, size_t len);

    Transport& transport_;
    PacketCb   on_video_;
    Callback   on_opened_;
    Callback   on_kf_req_;

    // Send side
    uint16_t           next_frame_id_ = 0;
    std::vector<uint8_t> chunk_buf_;

    // Receive side reassembly
    struct FrameAssembly {
        std::vector<std::vector<uint8_t>> chunks;
        uint16_t total    = 0;
        uint16_t received = 0;
    };
    std::mutex                                  assembly_mutex_;
    std::unordered_map<uint16_t, FrameAssembly> assembly_;
    static constexpr size_t kMaxAssemblyFrames  = 8;
};
