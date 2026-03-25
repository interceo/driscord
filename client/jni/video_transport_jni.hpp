#pragma once

#include "jni_common.hpp"
#include "video_transport.hpp"

#include <mutex>

struct VideoTransportJni {
    std::mutex  cb_mutex;
    JniCallback on_streaming_peer;
    JniCallback on_streaming_peer_removed;

    explicit VideoTransportJni(VideoTransport& video_transport);
};
