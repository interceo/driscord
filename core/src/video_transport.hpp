#pragma once

#include "transport.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>

class VideoTransport {
public:
    using PacketCb = Transport::PacketCb;
    using Callback = std::function<void()>;
    using VideoPacketCb = std::function<void(const std::string&, const uint8_t*, size_t, uint64_t)>;
    using KeyframeCb = Callback;

    // Rejects absurd/malicious frames before they reach the decoder. Frames are
    // sent whole, so this is a plain size cap rather than a chunk-count one.
    static constexpr size_t kMaxFrameBytes = 4 * 1024 * 1024;

    explicit VideoTransport(Transport& transport);

    void send_video(const uint8_t* data, size_t len);
    void send_keyframe_request();
    void send_stop_stream();

    // Streaming peer lifecycle — fired when a peer starts/stops sending video.
    void on_new_streaming_peer(std::function<void(const std::string&)> cb);
    void on_streaming_peer_removed(std::function<void(const std::string&)> cb);
    void remove_streaming_peer(const std::string& peer_id);

    // Watching gate — only routes incoming video from explicitly added peers to
    // the sink. Who the *server* sends us video for is driven separately by
    // Transport::send_watch_start/stop.
    void add_watched_peer(const std::string& peer_id);
    void remove_watched_peer(const std::string& peer_id);
    void clear_watched_peers();
    bool watching() const;

    // Video sink — set by whichever component consumes incoming video.
    void set_video_sink(VideoPacketCb video_cb, KeyframeCb kf_cb);
    void clear_video_sink();

    // Frames dropped because the send buffer was over the backpressure limit.
    uint64_t frames_dropped_backpressure() const
    {
        return frames_dropped_backpressure_;
    }

private:
    void on_frame(const std::string& peer_id, const uint8_t* data, size_t len);
    void on_control(const std::string& peer_id, const uint8_t* data, size_t len);
    void on_assembled(const std::string& peer_id,
        const uint8_t* data,
        size_t len,
        uint64_t frame_id);

    Transport& transport_;

    std::unordered_set<std::string> watched_peers_; // guarded by sink_mutex_

    std::mutex streaming_mutex_;
    std::set<std::string> seen_streaming_;
    std::function<void(const std::string&)> on_new_streaming_peer_;
    std::function<void(const std::string&)> on_streaming_peer_removed_;

    mutable std::mutex sink_mutex_;
    VideoPacketCb on_video_sink_;
    KeyframeCb on_keyframe_needed_;

    uint64_t next_frame_id_ = 0;
    std::atomic<uint64_t> frames_dropped_backpressure_ { 0 };
};
