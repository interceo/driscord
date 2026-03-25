#pragma once

#include "jni_common.hpp"
#include "driscord_core.hpp"
#include "video/screen_session.hpp"

#include <mutex>

// JNI callback adapter for ScreenSession events.
// Wires ScreenSession::on_frame / on_frame_removed to Java callbacks.
struct ScreenSessionJni {
    std::mutex  cb_mutex;
    JniCallback on_frame_cb;
    JniCallback on_frame_removed_cb;

    ScreenSessionJni(ScreenSession& session, DriscordCore& core);

    void fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h);
    void fire_remove_frame(const std::string& peer_id);
};
