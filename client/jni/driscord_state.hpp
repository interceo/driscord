#pragma once

#include "driscord_core.hpp"
#include "jni_common.hpp"

#include <mutex>

// Global singleton: DriscordCore + all JNI callback slots.
struct DriscordState {
    DriscordCore core;

    // Transport callbacks
    std::mutex  transport_mtx;
    JniCallback on_peer_joined;
    JniCallback on_peer_left;

    // VideoTransport callbacks
    std::mutex  video_mtx;
    JniCallback on_streaming_peer;
    JniCallback on_streaming_peer_removed;

    // ScreenSession callbacks (populated lazily by NativeScreenSession.init)
    std::mutex  screen_mtx;
    JniCallback on_frame_cb;
    JniCallback on_frame_removed_cb;

    DriscordState();
    static DriscordState& get();
};
