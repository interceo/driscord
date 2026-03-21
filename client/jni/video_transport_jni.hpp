#pragma once

#include "jni_common.hpp"
#include "transport_jni.hpp"
#include "video_transport.hpp"

#include <mutex>
#include <string>

struct VideoTransportJni {
    VideoTransport channel;

    // JNI callbacks — streaming peer lifecycle
    std::mutex cb_mutex;
    JniCallback on_streaming_peer;          // fires once per new streaming peer id
    JniCallback on_streaming_peer_removed;  // fires when a streaming peer is removed

    explicit VideoTransportJni(TransportJni& t);
};
