#pragma once

#include "jni_common.hpp"
#include "video_transport.hpp"
#include "transport_jni.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>

struct VideoTransportJni {
    VideoTransport channel;
    std::atomic<bool> watching{false};

    // JNI callbacks — streaming peer lifecycle
    std::mutex  cb_mutex;
    JniCallback on_streaming_peer;         // fires once per new streaming peer id
    JniCallback on_streaming_peer_removed; // fires when a streaming peer is removed

    std::mutex            streaming_mutex;
    std::set<std::string> seen_streaming;

    // Generic video sink — set by whichever component consumes incoming video
    // (ScreenSessionJni, WebcamSessionJni, …). Guarded by sink_mutex.
    using VideoPacketCb = std::function<void(const std::string&, const uint8_t*, size_t)>;
    using KeyframeCb    = std::function<void()>;

    std::mutex    sink_mutex;
    VideoPacketCb on_video_packet;   // called for every incoming video packet while watching
    KeyframeCb    on_keyframe_needed; // called on keyframe request or channel open

    void set_video_sink(VideoPacketCb video_cb, KeyframeCb kf_cb) {
        std::scoped_lock lk(sink_mutex);
        on_video_packet   = std::move(video_cb);
        on_keyframe_needed = std::move(kf_cb);
    }

    void clear_video_sink() {
        std::scoped_lock lk(sink_mutex);
        on_video_packet   = nullptr;
        on_keyframe_needed = nullptr;
    }

    explicit VideoTransportJni(TransportJni& t) : channel(t.transport) {
        channel.on_video_received([this](const std::string& peer_id,
                                         const uint8_t* data, size_t len) {
            {
                std::scoped_lock lk(streaming_mutex);
                if (seen_streaming.insert(peer_id).second) {
                    fire_string(on_streaming_peer, cb_mutex, peer_id);
                }
            }
            if (watching.load(std::memory_order_relaxed)) {
                std::scoped_lock lk(sink_mutex);
                if (on_video_packet) on_video_packet(peer_id, data, len);
            }
        });
        channel.on_keyframe_requested([this]() {
            std::scoped_lock lk(sink_mutex);
            if (on_keyframe_needed) on_keyframe_needed();
        });
        channel.on_video_channel_opened([this]() {
            std::scoped_lock lk(sink_mutex);
            if (on_keyframe_needed) on_keyframe_needed();
        });
    }

    void remove_streaming_peer(const std::string& peer_id) {
        bool was_present;
        {
            std::scoped_lock lk(streaming_mutex);
            was_present = seen_streaming.erase(peer_id) > 0;
        }
        if (was_present) {
            fire_string(on_streaming_peer_removed, cb_mutex, peer_id);
        }
    }
};
