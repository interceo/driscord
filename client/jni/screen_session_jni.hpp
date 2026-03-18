#pragma once

#include "audio_transport_jni.hpp"
#include "jni_common.hpp"
#include "video/screen_session.hpp"
#include "video_transport_jni.hpp"

#include <mutex>

// Thin JNI adapter that owns a ScreenSession and exposes Java callbacks
// (on_frame / on_frame_removed). All business logic lives in ScreenSession.
struct ScreenSessionJni {
    ScreenSession      session;
    VideoTransportJni* video_transport;  // non-owning, needed for clear_video_sink()

    std::mutex  cb_mutex;
    JniCallback on_frame_cb;
    JniCallback on_frame_removed_cb;

    ScreenSessionJni(int buf_ms, int max_sync_ms,
                     VideoTransportJni* vt, AudioTransportJni* at);

    void fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h);
    void fire_remove_frame(const std::string& peer_id);
};
