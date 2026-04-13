#include "driscord_jni_vulkan.hpp"
#include "driscord_state.hpp"
#include "jni_common.hpp"

#define CORE() DriscordState::get().core

extern "C" {

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenInit(JNIEnv*, jclass)
{
    CORE().init_screen_session();
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenDeinit(JNIEnv*, jclass)
{
    CORE().deinit_screen_session();
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_screenStartSharing(JNIEnv* env,
    jclass,
    jstring jTargetJson,
    jint maxW,
    jint maxH,
    jint fps,
    jboolean shareAudio)
{
    auto targetJson = jni_jstring_to_utf8(env, jTargetJson);
    auto r = CORE().screen_start_sharing(
        targetJson, static_cast<int>(maxW), static_cast<int>(maxH),
        static_cast<int>(fps), shareAudio == JNI_TRUE);
    return r ? nullptr : env->NewStringUTF(to_string(r.error()));
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenStopSharing(JNIEnv*, jclass)
{
    CORE().screen_stop_sharing();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_screenSharing(JNIEnv*, jclass)
{
    return CORE().screen_session->sharing() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_screenSharingAudio(JNIEnv*, jclass)
{
    return CORE().screen_session->sharing_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenForceKeyframe(JNIEnv*, jclass)
{
    CORE().screen_session->force_keyframe();
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenUpdate(JNIEnv*, jclass)
{
    CORE().screen_session->update();
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_screenActivePeer(JNIEnv* env, jclass)
{
    return env->NewStringUTF(CORE().screen_session->active_peer().c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_screenActive(JNIEnv*, jclass)
{
    return CORE().screen_session->active() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenReset(JNIEnv*, jclass)
{
    CORE().screen_session->reset();
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenResetAudioReceiver(JNIEnv*, jclass)
{
    CORE().screen_session->reset_audio();
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenSetStreamVolume(JNIEnv* env,
    jclass,
    jstring jPeerId,
    jfloat vol)
{
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().screen_set_stream_volume(peer, static_cast<float>(vol));
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_jni_NativeDriscord_screenStreamVolume(JNIEnv*, jclass)
{
    return CORE().screen_stream_volume();
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_screenStats(JNIEnv* env, jclass)
{
    return env->NewStringUTF(CORE().screen_session->stats_json().c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_setOnFrame(JNIEnv* env,
    jclass,
    jobject cb)
{
    if (!cb) {
        CORE().set_on_frame(nullptr);
        return;
    }
    // Pass the native RGBA pointer directly (as jlong) instead of copying into
    // a JVM byte array.  The Kotlin side must create a Skia snapshot from the
    // pointer synchronously before this callback returns, because the underlying
    // buffer is owned by the C++ VideoReceiver and may be reused on the next
    // update cycle.
    auto ref = std::make_shared<JniRef>(env, cb, "invoke", "(Ljava/lang/String;JII)V");
    CORE().set_on_frame(
        [ref](const std::string& pid, const uint8_t* rgba, int w, int h) {
            // Vulkan fast path: render directly if a renderer is attached.
            if (vulkan_try_present(pid, rgba, w, h)) return;

            // Skia fallback: pass native RGBA pointer to Kotlin.
            auto* e = ref->attach();
            if (!e) {
                return;
            }
            jstring jpeer = e->NewStringUTF(pid.c_str());
            auto jptr = static_cast<jlong>(reinterpret_cast<uintptr_t>(rgba));
            e->CallVoidMethod(ref->obj, ref->mid, jpeer, jptr, (jint)w, (jint)h);
            e->DeleteLocalRef(jpeer);
        });
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_setOnFrameRemoved(JNIEnv* env,
    jclass,
    jobject cb)
{
    CORE().set_on_frame_removed(make_string_cb(env, cb));
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenJoinStream(JNIEnv* env,
    jclass,
    jstring jPeerId)
{
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().join_stream(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_screenLeaveStream(JNIEnv*, jclass)
{
    CORE().leave_stream();
}

JNIEXPORT jbyteArray JNICALL
Java_com_driscord_jni_NativeDriscord_copyNativePixels(JNIEnv* env, jclass,
    jlong ptr, jint size)
{
    auto arr = env->NewByteArray(size);
    env->SetByteArrayRegion(arr, 0, size,
        reinterpret_cast<const jbyte*>(static_cast<uintptr_t>(ptr)));
    return arr;
}

} // extern "C"
