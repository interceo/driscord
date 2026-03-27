#include "driscord_state.hpp"
#include "jni_common.hpp"

#define CORE() DriscordState::get().core

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_captureSystemAudioAvailable(JNIEnv*, jclass) {
    return CORE().capture_system_audio_available() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_captureListTargets(JNIEnv* env, jclass) {
    return env->NewStringUTF(CORE().capture_list_targets_json().c_str());
}

JNIEXPORT jbyteArray JNICALL
Java_com_driscord_jni_NativeDriscord_captureGrabThumbnail(JNIEnv* env, jclass,
        jstring jTargetJson, jint maxW, jint maxH) {
    const char* raw = env->GetStringUTFChars(jTargetJson, nullptr);
    auto rgba = CORE().capture_grab_thumbnail(raw, static_cast<int>(maxW), static_cast<int>(maxH));
    env->ReleaseStringUTFChars(jTargetJson, raw);

    if (rgba.empty()) return nullptr;

    jbyteArray out = env->NewByteArray(static_cast<jsize>(rgba.size()));
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(rgba.size()),
                            reinterpret_cast<const jbyte*>(rgba.data()));
    return out;
}

} // extern "C"
