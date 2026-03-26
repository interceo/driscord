#include "driscord_state.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static void fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h) {
    auto& s = DriscordState::get();
    std::scoped_lock lk(s.screen_mtx);
    if (!s.on_frame_cb.obj) return;
    auto* env = s.on_frame_cb.attach();
    if (!env) return;
    jstring jpeer    = env->NewStringUTF(peer_id.c_str());
    jbyteArray jdata = env->NewByteArray(w * h * 4);
    env->SetByteArrayRegion(jdata, 0, w * h * 4, reinterpret_cast<const jbyte*>(rgba));
    env->CallVoidMethod(s.on_frame_cb.obj, s.on_frame_cb.mid, jpeer, jdata, (jint)w, (jint)h);
    env->DeleteLocalRef(jdata);
    env->DeleteLocalRef(jpeer);
}

static void fire_remove_frame(const std::string& peer_id) {
    auto& s = DriscordState::get();
    std::scoped_lock lk(s.screen_mtx);
    if (!s.on_frame_removed_cb.obj) return;
    auto* env = s.on_frame_removed_cb.attach();
    if (!env) return;
    jstring jpeer = env->NewStringUTF(peer_id.c_str());
    env->CallVoidMethod(s.on_frame_removed_cb.obj, s.on_frame_removed_cb.mid, jpeer);
    env->DeleteLocalRef(jpeer);
}

#define CORE() DriscordState::get().core
#define SS()   (*CORE().screen_session)

extern "C" {

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_init(
    JNIEnv*, jclass, jint bufMs, jint maxSyncMs
) {
    CORE().init_screen_session(static_cast<int>(bufMs), static_cast<int>(maxSyncMs));
    SS().set_on_frame([](const std::string& pid, const uint8_t* rgba, int w, int h) {
        fire_frame(pid, rgba, w, h);
    });
    SS().set_on_frame_removed([](const std::string& pid) {
        DriscordState::get().core.on_video_peer_stream_ended(pid);
        fire_remove_frame(pid);
    });
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_deinit(JNIEnv*, jclass) {
    CORE().deinit_screen_session();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeScreenSession_startSharing(
    JNIEnv* env, jclass,
    jstring jTargetJson, jint maxW, jint maxH, jint fps, jint bitrateKbps,
    jint /*gopSize*/, jboolean shareAudio
) {
    const char* raw = env->GetStringUTFChars(jTargetJson, nullptr);
    auto target = CaptureTarget::from_json(json::parse(raw));
    env->ReleaseStringUTFChars(jTargetJson, raw);

    bool ok = SS().start_sharing(
        target,
        static_cast<int>(maxW), static_cast<int>(maxH),
        static_cast<int>(fps),  static_cast<int>(bitrateKbps),
        shareAudio == JNI_TRUE
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_stopSharing(JNIEnv*, jclass) {
    SS().stop_sharing();
    CORE().video_transport.send_stop_stream();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeScreenSession_sharing(JNIEnv*, jclass) {
    return SS().sharing() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeScreenSession_sharingAudio(JNIEnv*, jclass) {
    return SS().sharing_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_forceKeyframe(JNIEnv*, jclass) {
    SS().force_keyframe();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_update(JNIEnv*, jclass) {
    SS().update();
}

JNIEXPORT jstring JNICALL Java_com_driscord_jni_NativeScreenSession_activePeer(
    JNIEnv* env, jclass
) {
    return env->NewStringUTF(SS().active_peer().c_str());
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeScreenSession_active(JNIEnv*, jclass) {
    return SS().active() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_reset(JNIEnv*, jclass) {
    SS().reset();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_resetAudioReceiver(
    JNIEnv*, jclass
) {
    SS().reset_audio();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_setStreamVolume(
    JNIEnv* env, jclass, jstring jPeerId, jfloat vol
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().audio_transport.set_screen_audio_peer_volume(peer, static_cast<float>(vol));
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeScreenSession_streamVolume(JNIEnv*, jclass) {
    return CORE().audio_transport.screen_audio_peer_volume(SS().active_peer());
}

JNIEXPORT jstring JNICALL Java_com_driscord_jni_NativeScreenSession_stats(JNIEnv* env, jclass) {
    return env->NewStringUTF(SS().stats_json().c_str());
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_setOnFrame(
    JNIEnv* env, jclass, jobject cb
) {
    auto& s = DriscordState::get();
    std::scoped_lock lk(s.screen_mtx);
    set_callback(env, s.on_frame_cb, cb, "invoke", "(Ljava/lang/String;[BII)V");
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_setOnFrameRemoved(
    JNIEnv* env, jclass, jobject cb
) {
    auto& s = DriscordState::get();
    std::scoped_lock lk(s.screen_mtx);
    set_callback(env, s.on_frame_removed_cb, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_joinStream(
    JNIEnv* env, jclass, jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().join_stream(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_leaveStream(JNIEnv*, jclass) {
    CORE().leave_stream();
}

} // extern "C"
