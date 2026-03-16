#pragma once

#include "jni_common.hpp"
#include "transport_jni.hpp"
#include "video_transport.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>

struct VideoTransportJni {
    VideoTransport channel;
    std::atomic<bool> watching{false};

    // JNI callbacks — streaming peer lifecycle
    std::mutex cb_mutex;
    JniCallback on_streaming_peer;          // fires once per new streaming peer id
    JniCallback on_streaming_peer_removed;  // fires when a streaming peer is removed

    std::mutex streaming_mutex;
    std::set<std::string> seen_streaming;

    // Generic video sink — set by whichever component consumes incoming video
    // (ScreenSessionJni, WebcamSessionJni, …). Guarded by sink_mutex.
    using VideoPacketCb = std::function<void(const std::string&, const uint8_t*, size_t)>;
    using KeyframeCb = std::function<void()>;

    std::mutex sink_mutex;
    VideoPacketCb on_video_packet;  // called for every incoming video packet while watching
    KeyframeCb on_keyframe_needed;  // called on keyframe request or channel open

    explicit VideoTransportJni(TransportJni& t);

    void remove_streaming_peer(const std::string& peer_id);

    void set_video_sink(VideoPacketCb video_cb, KeyframeCb kf_cb);
    void clear_video_sink();
};
