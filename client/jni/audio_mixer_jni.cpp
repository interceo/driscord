#include "audio_receiver_jni.hpp"
#include "audio/audio_mixer.hpp"

#define AUDIO_MIXER(h)    reinterpret_cast<AudioMixer*>(h)
#define AUDIO_RECEIVER(h) reinterpret_cast<AudioReceiverJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeAudioMixer_create(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new AudioMixer());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_destroy(JNIEnv*, jclass, jlong h) {
    delete AUDIO_MIXER(h);
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeAudioMixer_start(JNIEnv*, jclass, jlong h) {
    return AUDIO_MIXER(h)->start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_stop(JNIEnv*, jclass, jlong h) {
    AUDIO_MIXER(h)->stop();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeAudioMixer_deafened(JNIEnv*, jclass, jlong h) {
    return AUDIO_MIXER(h)->deafened() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_setDeafened(JNIEnv*, jclass, jlong h, jboolean deaf) {
    AUDIO_MIXER(h)->set_deafened(deaf == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeAudioMixer_outputVolume(JNIEnv*, jclass, jlong h) {
    return AUDIO_MIXER(h)->output_volume();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_setOutputVolume(JNIEnv*, jclass, jlong h, jfloat vol) {
    AUDIO_MIXER(h)->set_output_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeAudioMixer_outputLevel(JNIEnv*, jclass, jlong h) {
    return AUDIO_MIXER(h)->output_level();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_addSource(JNIEnv*, jclass, jlong h, jlong receiverHandle) {
    AUDIO_MIXER(h)->add_source(&AUDIO_RECEIVER(receiverHandle)->receiver);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_removeSource(JNIEnv*, jclass, jlong h, jlong receiverHandle) {
    AUDIO_MIXER(h)->remove_source(&AUDIO_RECEIVER(receiverHandle)->receiver);
}

} // extern "C"
