#pragma once

#include "jni_common.hpp"
#include "video/screen_session.hpp"
#include "video_transport_jni.hpp"
#include "audio_transport_jni.hpp"

#include <mutex>
#include <string>

struct ScreenSessionJni {
    ScreenSession      session;
    VideoTransportJni* video_transport; // non-owning
    AudioTransportJni* audio_transport; // non-owning

    std::mutex  cb_mutex;
    JniCallback on_frame_cb;
    JniCallback on_frame_removed_cb;

    std::string last_peer;
    int last_w = 0, last_h = 0;

    ScreenSessionJni(int buf_ms, int max_sync_ms,
                     VideoTransportJni* vt, AudioTransportJni* at)
        : session(buf_ms, max_sync_ms), video_transport(vt), audio_transport(at)
    {
        session.set_keyframe_callback([this]() {
            video_transport->channel.send_keyframe_request();
        });
        video_transport->screen_session = &session;
    }

    bool start_sharing(const CaptureTarget& target, int max_w, int max_h,
                       int fps, int bitrate_kbps, bool share_audio) {
        return session.start_sharing(
            target, max_w, max_h, fps, bitrate_kbps, share_audio,
            [this](const uint8_t* d, size_t l) { video_transport->channel.send_video(d, l); },
            [this](const uint8_t* d, size_t l) { audio_transport->channel.send_screen_audio(d, l); }
        );
    }

    void update() {
        if (auto* frame = session.update()) {
            std::string peer = session.active_peer();
            if (!peer.empty()) {
                if (!last_peer.empty() && last_peer != peer)
                    fire_remove_frame(last_peer);
                last_w = frame->width;
                last_h = frame->height;
                fire_frame(peer, frame->rgba.data(), frame->width, frame->height);
                last_peer = peer;
            }
        }
        if (!last_peer.empty() && !session.active()) {
            fire_remove_frame(last_peer);
            last_peer.clear();
        }
    }

    void fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h) {
        std::scoped_lock lk(cb_mutex);
        if (!on_frame_cb.obj) return;
        auto* env = on_frame_cb.attach();
        if (!env) return;
        jstring jpeer = env->NewStringUTF(peer_id.c_str());
        jbyteArray jdata = env->NewByteArray(w * h * 4);
        env->SetByteArrayRegion(jdata, 0, w * h * 4, reinterpret_cast<const jbyte*>(rgba));
        env->CallVoidMethod(on_frame_cb.obj, on_frame_cb.mid, jpeer, jdata, (jint)w, (jint)h);
        env->DeleteLocalRef(jdata);
        env->DeleteLocalRef(jpeer);
    }

    void fire_remove_frame(const std::string& peer_id) {
        std::scoped_lock lk(cb_mutex);
        if (!on_frame_removed_cb.obj) return;
        auto* env = on_frame_removed_cb.attach();
        if (!env) return;
        jstring jpeer = env->NewStringUTF(peer_id.c_str());
        env->CallVoidMethod(on_frame_removed_cb.obj, on_frame_removed_cb.mid, jpeer);
        env->DeleteLocalRef(jpeer);
    }
};
