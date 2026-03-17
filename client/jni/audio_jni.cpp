#include "audio_jni.hpp"

#define AUDIO_SENDER(h) reinterpret_cast<AudioSenderJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL Java_com_driscord_jni_NativeAudioSender_create(JNIEnv*, jclass, jlong audioTransportHandle) {
    return reinterpret_cast<jlong>(new AudioSenderJni(reinterpret_cast<AudioTransportJni*>(audioTransportHandle)));
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioSender_destroy(JNIEnv*, jclass, jlong h) { delete AUDIO_SENDER(h); }

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeAudioSender_start(JNIEnv*, jclass, jlong h) {
    return AUDIO_SENDER(h)->start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioSender_stop(JNIEnv*, jclass, jlong h) {
    AUDIO_SENDER(h)->sender.stop();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeAudioSender_muted(JNIEnv*, jclass, jlong h) {
    return AUDIO_SENDER(h)->sender.muted() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioSender_setMuted(JNIEnv*, jclass, jlong h, jboolean muted) {
    AUDIO_SENDER(h)->sender.set_muted(muted == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeAudioSender_inputLevel(JNIEnv*, jclass, jlong h) {
    return AUDIO_SENDER(h)->sender.input_level();
}



/*----------------------------------------------*/

#define AUDIO_RECEIVER(h) reinterpret_cast<AudioReceiverJni*>(h)

JNIEXPORT jlong JNICALL Java_com_driscord_jni_NativeAudioReceiver_create(JNIEnv*, jclass, jint jitterMs) {
    return reinterpret_cast<jlong>(new AudioReceiverJni(static_cast<int>(jitterMs)));
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioReceiver_destroy(JNIEnv*, jclass, jlong h) {
    delete AUDIO_RECEIVER(h);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioReceiver_reset(JNIEnv*, jclass, jlong h) {
    AUDIO_RECEIVER(h)->receiver->reset();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioReceiver_setVolume(JNIEnv*, jclass, jlong h, jfloat vol) {
    AUDIO_RECEIVER(h)->receiver->set_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeAudioReceiver_volume(JNIEnv*, jclass, jlong h) {
    return AUDIO_RECEIVER(h)->receiver->volume();
}

}  // extern "C"
