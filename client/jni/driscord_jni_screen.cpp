#include "driscord_state.hpp"
#include "jni_common.hpp"

#define CORE() DriscordState::get().core

extern "C" {

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenInit(
    JNIEnv*, jclass, jint bufMs, jint maxSyncMs
) {
    CORE().init_screen_session(static_cast<int>(bufMs), static_cast<int>(maxSyncMs));
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenDeinit(JNIEnv*, jclass) {
    CORE().deinit_screen_session();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeDriscord_screenStartSharing(
    JNIEnv* env, jclass,
    jstring jTargetJson, jint maxW, jint maxH, jint fps, jint bitrateKbps,
    jint /*gopSize*/, jboolean shareAudio
) {
    auto targetJson = jni_jstring_to_utf8(env, jTargetJson);
    bool ok = CORE().screen_start_sharing(targetJson, static_cast<int>(maxW), static_cast<int>(maxH),
        static_cast<int>(fps), static_cast<int>(bitrateKbps), shareAudio == JNI_TRUE);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenStopSharing(JNIEnv*, jclass) {
    CORE().screen_stop_sharing();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeDriscord_screenSharing(JNIEnv*, jclass) {
    return CORE().screen_sharing() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeDriscord_screenSharingAudio(JNIEnv*, jclass) {
    return CORE().screen_sharing_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenForceKeyframe(JNIEnv*, jclass) {
    CORE().screen_force_keyframe();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenUpdate(JNIEnv*, jclass) {
    CORE().screen_update();
}

JNIEXPORT jstring JNICALL Java_com_driscord_jni_NativeDriscord_screenActivePeer(JNIEnv* env, jclass) {
    return env->NewStringUTF(CORE().screen_active_peer().c_str());
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeDriscord_screenActive(JNIEnv*, jclass) {
    return CORE().screen_active() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenReset(JNIEnv*, jclass) {
    CORE().screen_reset();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenResetAudioReceiver(JNIEnv*, jclass) {
    CORE().screen_reset_audio();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenSetStreamVolume(
    JNIEnv* env, jclass, jstring jPeerId, jfloat vol
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().screen_set_stream_volume(peer, static_cast<float>(vol));
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeDriscord_screenStreamVolume(JNIEnv*, jclass) {
    return CORE().screen_stream_volume();
}

JNIEXPORT jstring JNICALL Java_com_driscord_jni_NativeDriscord_screenStats(JNIEnv* env, jclass) {
    return env->NewStringUTF(CORE().screen_stats_json().c_str());
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_setOnFrame(
    JNIEnv* env, jclass, jobject cb
) {
    if (!cb) {
        CORE().set_on_frame(nullptr);
        return;
    }
    auto ref = std::make_shared<JniRef>(env, cb, "invoke", "(Ljava/lang/String;[BII)V");
    CORE().set_on_frame([ref](const std::string& pid, const uint8_t* rgba, int w, int h) {
        auto* e = ref->attach();
        if (!e) return;
        jstring jpeer    = e->NewStringUTF(pid.c_str());
        jbyteArray jdata = e->NewByteArray(w * h * 4);
        e->SetByteArrayRegion(jdata, 0, w * h * 4, reinterpret_cast<const jbyte*>(rgba));
        e->CallVoidMethod(ref->obj, ref->mid, jpeer, jdata, (jint)w, (jint)h);
        e->DeleteLocalRef(jdata);
        e->DeleteLocalRef(jpeer);
    });
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_setOnFrameRemoved(
    JNIEnv* env, jclass, jobject cb
) {
    CORE().set_on_frame_removed(make_string_cb(env, cb));
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenJoinStream(
    JNIEnv* env, jclass, jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().join_stream(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_screenLeaveStream(JNIEnv*, jclass) {
    CORE().leave_stream();
}

} // extern "C"
