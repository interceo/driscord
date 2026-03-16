#include "screen_session_jni.hpp"
#include "audio/audio_mixer.hpp"
#include "audio_receiver_jni.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

ScreenSessionJni::ScreenSessionJni(int buf_ms, int max_sync_ms, VideoTransportJni* vt, AudioTransportJni* at)
    : session(buf_ms, max_sync_ms), video_transport(vt), audio_transport(at) {
    session.set_keyframe_callback([this]() { video_transport->channel.send_keyframe_request(); });
    video_transport->set_video_sink(
        [this](const std::string& peer_id, const uint8_t* data, size_t len) {
            session.push_video_packet(peer_id, data, len);
        },
        [this]() {
            if (session.sharing()) {
                session.force_keyframe();
            }
        }
    );
}

bool ScreenSessionJni::start_sharing(
    const CaptureTarget& target,
    int max_w,
    int max_h,
    int fps,
    int bitrate_kbps,
    bool share_audio
) {
    return session.start_sharing(
        target,
        max_w,
        max_h,
        fps,
        bitrate_kbps,
        share_audio,
        [this](const uint8_t* d, size_t l) { video_transport->channel.send_video(d, l); },
        [this](const uint8_t* d, size_t l) { audio_transport->channel.send_screen_audio(d, l); }
    );
}

void ScreenSessionJni::update() {
    if (auto* frame = session.update()) {
        std::string peer = session.active_peer();
        if (!peer.empty()) {
            if (!last_peer.empty() && last_peer != peer) {
                fire_remove_frame(last_peer);
            }
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

void ScreenSessionJni::fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h) {
    std::scoped_lock lk(cb_mutex);
    if (!on_frame_cb.obj) {
        return;
    }
    auto* env = on_frame_cb.attach();
    if (!env) {
        return;
    }
    jstring jpeer = env->NewStringUTF(peer_id.c_str());
    jbyteArray jdata = env->NewByteArray(w * h * 4);
    env->SetByteArrayRegion(jdata, 0, w * h * 4, reinterpret_cast<const jbyte*>(rgba));
    env->CallVoidMethod(on_frame_cb.obj, on_frame_cb.mid, jpeer, jdata, (jint)w, (jint)h);
    env->DeleteLocalRef(jdata);
    env->DeleteLocalRef(jpeer);
}

void ScreenSessionJni::fire_remove_frame(const std::string& peer_id) {
    std::scoped_lock lk(cb_mutex);
    if (!on_frame_removed_cb.obj) {
        return;
    }
    auto* env = on_frame_removed_cb.attach();
    if (!env) {
        return;
    }
    jstring jpeer = env->NewStringUTF(peer_id.c_str());
    env->CallVoidMethod(on_frame_removed_cb.obj, on_frame_removed_cb.mid, jpeer);
    env->DeleteLocalRef(jpeer);
}

#define SCREEN_SESSION(h) reinterpret_cast<ScreenSessionJni*>(h)
#define AUDIO_MIXER(h) reinterpret_cast<AudioMixer*>(h)

extern "C" {

JNIEXPORT jlong JNICALL Java_com_driscord_NativeScreenSession_create(
    JNIEnv*,
    jclass,
    jint bufMs,
    jint maxSyncMs,
    jlong videoTransportHandle,
    jlong audioTransportHandle
) {
    return reinterpret_cast<jlong>(new ScreenSessionJni(
        static_cast<int>(bufMs),
        static_cast<int>(maxSyncMs),
        reinterpret_cast<VideoTransportJni*>(videoTransportHandle),
        reinterpret_cast<AudioTransportJni*>(audioTransportHandle)
    ));
}

JNIEXPORT void JNICALL Java_com_driscord_NativeScreenSession_destroy(JNIEnv*, jclass, jlong h) {
    auto* s = SCREEN_SESSION(h);
    s->video_transport->clear_video_sink();
    delete s;
}

JNIEXPORT jboolean JNICALL Java_com_driscord_NativeScreenSession_startSharing(
    JNIEnv* env,
    jclass,
    jlong h,
    jstring jTargetJson,
    jint maxW,
    jint maxH,
    jint fps,
    jint bitrateKbps,
    jboolean shareAudio
) {
    const char* raw = env->GetStringUTFChars(jTargetJson, nullptr);
    CaptureTarget target = target_from_json(json::parse(raw));
    env->ReleaseStringUTFChars(jTargetJson, raw);
    bool ok = SCREEN_SESSION(h)->start_sharing(
        target,
        static_cast<int>(maxW),
        static_cast<int>(maxH),
        static_cast<int>(fps),
        static_cast<int>(bitrateKbps),
        shareAudio == JNI_TRUE
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_NativeScreenSession_stopSharing(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->session.stop_sharing();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_NativeScreenSession_sharing(JNIEnv*, jclass, jlong h) {
    return SCREEN_SESSION(h)->session.sharing() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_driscord_NativeScreenSession_sharingAudio(JNIEnv*, jclass, jlong h) {
    return SCREEN_SESSION(h)->session.sharing_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_NativeScreenSession_forceKeyframe(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->session.force_keyframe();
}

JNIEXPORT void JNICALL Java_com_driscord_NativeScreenSession_update(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->update();
}

JNIEXPORT jstring JNICALL Java_com_driscord_NativeScreenSession_activePeer(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(SCREEN_SESSION(h)->session.active_peer().c_str());
}

JNIEXPORT jboolean JNICALL Java_com_driscord_NativeScreenSession_active(JNIEnv*, jclass, jlong h) {
    return SCREEN_SESSION(h)->session.active() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_NativeScreenSession_reset(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->session.reset();
    SCREEN_SESSION(h)->last_peer.clear();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_addAudioReceiverToMixer(JNIEnv*, jclass, jlong screenHandle, jlong mixerHandle) {
    AUDIO_MIXER(mixerHandle)->add_source(SCREEN_SESSION(screenHandle)->session.audio_receiver());
}

JNIEXPORT void JNICALL Java_com_driscord_NativeScreenSession_removeAudioReceiverFromMixer(
    JNIEnv*,
    jclass,
    jlong screenHandle,
    jlong mixerHandle
) {
    AUDIO_MIXER(mixerHandle)->remove_source(SCREEN_SESSION(screenHandle)->session.audio_receiver());
}

JNIEXPORT void JNICALL Java_com_driscord_NativeScreenSession_resetAudioReceiver(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->session.audio_receiver()->reset();
}

JNIEXPORT void JNICALL Java_com_driscord_NativeScreenSession_setStreamVolume(JNIEnv*, jclass, jlong h, jfloat vol) {
    SCREEN_SESSION(h)->session.audio_receiver()->set_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL Java_com_driscord_NativeScreenSession_streamVolume(JNIEnv*, jclass, jlong h) {
    return SCREEN_SESSION(h)->session.audio_receiver()->volume();
}

JNIEXPORT jstring JNICALL Java_com_driscord_NativeScreenSession_stats(JNIEnv* env, jclass, jlong h) {
    auto* s = SCREEN_SESSION(h);
    auto vs = s->session.video_stats();
    auto as = s->session.audio_stats();
    json j = {
        {"width", s->last_w},
        {"height", s->last_h},
        {"measuredKbps", s->session.measured_kbps()},
        {"video",
         {{"queue", vs.queue_size}, {"bufMs", vs.buffered_ms}, {"drops", vs.drop_count}, {"misses", vs.miss_count}}},
        {"audio",
         {{"queue", as.queue_size}, {"bufMs", as.buffered_ms}, {"drops", as.drop_count}, {"misses", as.miss_count}}}
    };
    return env->NewStringUTF(j.dump().c_str());
}

JNIEXPORT void JNICALL Java_com_driscord_NativeScreenSession_setOnFrame(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* s = SCREEN_SESSION(h);
    std::scoped_lock lk(s->cb_mutex);
    set_callback(env, s->on_frame_cb, cb, "invoke", "(Ljava/lang/String;[BII)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_setOnFrameRemoved(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* s = SCREEN_SESSION(h);
    std::scoped_lock lk(s->cb_mutex);
    set_callback(env, s->on_frame_removed_cb, cb, "invoke", "(Ljava/lang/String;)V");
}

}  // extern "C"
