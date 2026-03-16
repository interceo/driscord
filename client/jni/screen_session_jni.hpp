#pragma once

#include "audio_transport_jni.hpp"
#include "jni_common.hpp"
#include "video/screen_session.hpp"
#include "video_transport_jni.hpp"

#include <mutex>
#include <string>

struct ScreenSessionJni {
    ScreenSession session;
    VideoTransportJni* video_transport;  // non-owning
    AudioTransportJni* audio_transport;  // non-owning

    std::mutex cb_mutex;
    JniCallback on_frame_cb;
    JniCallback on_frame_removed_cb;

    std::string last_peer;
    int last_w = 0, last_h = 0;

    ScreenSessionJni(int buf_ms, int max_sync_ms, VideoTransportJni* vt, AudioTransportJni* at);

    bool start_sharing(const CaptureTarget& target, int max_w, int max_h, int fps, int bitrate_kbps, bool share_audio);

    void update();

    void fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h);

    void fire_remove_frame(const std::string& peer_id);
};
