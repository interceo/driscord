#include "driscord_state.hpp"
#include "jni_common.hpp"

#define CORE() DriscordState::get().core

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_captureSystemAudioAvailable(JNIEnv*, jclass) {
    return CORE().capture_system_audio_available() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_captureVideoListTargets(JNIEnv* env, jclass) {
    return jni_utf8_to_jstring(env, CORE().capture_video_list_targets_json());
}

JNIEXPORT jbyteArray JNICALL
Java_com_driscord_jni_NativeDriscord_captureGrabThumbnail(JNIEnv* env, jclass,
        jstring jTargetJson, jint maxW, jint maxH) {
    auto targetJson = jni_jstring_to_utf8(env, jTargetJson);
    auto rgba = CORE().capture_grab_thumbnail(targetJson, static_cast<int>(maxW), static_cast<int>(maxH));

    if (rgba.empty()) return nullptr;

    jbyteArray out = env->NewByteArray(static_cast<jsize>(rgba.size()));
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(rgba.size()),
                            reinterpret_cast<const jbyte*>(rgba.data()));
    return out;
}

} // extern "C"
