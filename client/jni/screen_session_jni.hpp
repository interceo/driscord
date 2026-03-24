#pragma once

#include "jni_common.hpp"
#include "video/screen_session.hpp"

#include <mutex>
#include <string>
#include <unordered_set>

// Thin JNI adapter that owns a ScreenSession and exposes Java callbacks
// (on_frame / on_frame_removed). All business logic lives in ScreenSession.
struct ScreenSessionJni {
    ScreenSession session;
    std::unordered_set<std::string> watched_peers;

    std::mutex  cb_mutex;
    JniCallback on_frame_cb;
    JniCallback on_frame_removed_cb;

    ScreenSessionJni(int buf_ms, int max_sync_ms);

    void join_stream(const std::string& peer_id);
    void leave_stream();

    void fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h);
    void fire_remove_frame(const std::string& peer_id);
};
