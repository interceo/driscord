#include "audio_sender_jni.hpp"

#define AUDIO_SENDER(h) reinterpret_cast<AudioSenderJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeAudioSender_create(JNIEnv*, jclass, jlong audioTransportHandle) {
    return reinterpret_cast<jlong>(
        new AudioSenderJni(reinterpret_cast<AudioTransportJni*>(audioTransportHandle)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioSender_destroy(JNIEnv*, jclass, jlong h) {
    delete AUDIO_SENDER(h);
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeAudioSender_start(JNIEnv*, jclass, jlong h) {
    return AUDIO_SENDER(h)->start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioSender_stop(JNIEnv*, jclass, jlong h) {
    AUDIO_SENDER(h)->sender.stop();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeAudioSender_muted(JNIEnv*, jclass, jlong h) {
    return AUDIO_SENDER(h)->sender.muted() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioSender_setMuted(JNIEnv*, jclass, jlong h, jboolean muted) {
    AUDIO_SENDER(h)->sender.set_muted(muted == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeAudioSender_inputLevel(JNIEnv*, jclass, jlong h) {
    return AUDIO_SENDER(h)->sender.input_level();
}

} // extern "C"
