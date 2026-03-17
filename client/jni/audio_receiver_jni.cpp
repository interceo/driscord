#include "audio_receiver_jni.hpp"

#define AUDIO_RECEIVER(h) reinterpret_cast<AudioReceiverJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeAudioReceiver_create(JNIEnv*, jclass, jint jitterMs) {
    return reinterpret_cast<jlong>(new AudioReceiverJni(static_cast<int>(jitterMs)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioReceiver_destroy(JNIEnv*, jclass, jlong h) {
    delete AUDIO_RECEIVER(h);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioReceiver_reset(JNIEnv*, jclass, jlong h) {
    AUDIO_RECEIVER(h)->receiver->reset();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioReceiver_setVolume(JNIEnv*, jclass, jlong h, jfloat vol) {
    AUDIO_RECEIVER(h)->receiver->set_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeAudioReceiver_volume(JNIEnv*, jclass, jlong h) {
    return AUDIO_RECEIVER(h)->receiver->volume();
}

} // extern "C"
