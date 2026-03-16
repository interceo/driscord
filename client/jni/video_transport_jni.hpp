#pragma once

#include "jni_common.hpp"
#include "video_transport.hpp"
#include "transport_jni.hpp"

#include <atomic>
#include <mutex>
#include <set>
#include <string>

class ScreenSession; // forward declaration — set by ScreenSessionJni

struct VideoTransportJni {
    VideoTransport channel;
    ScreenSession* screen_session = nullptr; // non-owning, set by ScreenSessionJni
    std::atomic<bool> watching{false};

    std::mutex  cb_mutex;
    JniCallback on_streaming_peer;         // fires once per new streaming peer id
    JniCallback on_streaming_peer_removed; // fires when a streaming peer is removed

    std::mutex            streaming_mutex;
    std::set<std::string> seen_streaming;

    explicit VideoTransportJni(TransportJni& t) : channel(t.transport) {
        channel.on_video_received([this](const std::string& peer_id,
                                         const uint8_t* data, size_t len) {
            {
                std::scoped_lock lk(streaming_mutex);
                if (seen_streaming.insert(peer_id).second) {
                    fire_string(on_streaming_peer, cb_mutex, peer_id);
                }
            }
            if (watching.load(std::memory_order_relaxed) && screen_session) {
                screen_session->push_video_packet(peer_id, data, len);
            }
        });
        channel.on_keyframe_requested([this]() {
            if (screen_session && screen_session->sharing())
                screen_session->force_keyframe();
        });
        channel.on_video_channel_opened([this]() {
            if (screen_session && screen_session->sharing())
                screen_session->force_keyframe();
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
