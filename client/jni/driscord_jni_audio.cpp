#include "driscord_state.hpp"
#include "jni_common.hpp"

#define CORE() DriscordState::get().core

extern "C" {

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioSend(
    JNIEnv* env, jclass, jbyteArray jdata, jint len
) {
    auto* buf = env->GetByteArrayElements(jdata, nullptr);
    CORE().audio_send(reinterpret_cast<const uint8_t*>(buf), len);
    env->ReleaseByteArrayElements(jdata, buf, JNI_ABORT);
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeDriscord_audioStart(JNIEnv*, jclass) {
    return CORE().audio_start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioStop(JNIEnv*, jclass) {
    CORE().audio_stop();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeDriscord_audioDeafened(JNIEnv*, jclass) {
    return CORE().audio_deafened() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioSetDeafened(
    JNIEnv*, jclass, jboolean deaf
) {
    CORE().audio_set_deafened(deaf == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeDriscord_audioMasterVolume(JNIEnv*, jclass) {
    return CORE().audio_master_volume();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioSetMasterVolume(
    JNIEnv*, jclass, jfloat vol
) {
    CORE().audio_set_master_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeDriscord_audioOutputLevel(JNIEnv*, jclass) {
    return CORE().audio_output_level();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeDriscord_audioSelfMuted(JNIEnv*, jclass) {
    return CORE().audio_self_muted() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioSetSelfMuted(
    JNIEnv*, jclass, jboolean muted
) {
    CORE().audio_set_self_muted(muted == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeDriscord_audioInputLevel(JNIEnv*, jclass) {
    return CORE().audio_input_level();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioOnPeerJoined(
    JNIEnv* env, jclass, jstring jPeer, jint jitterMs
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().audio_on_peer_joined(peer, static_cast<int>(jitterMs));
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioOnPeerLeft(
    JNIEnv* env, jclass, jstring jPeer
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().audio_on_peer_left(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioSetPeerVolume(
    JNIEnv* env, jclass, jstring jPeer, jfloat vol
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().audio_set_peer_volume(peer, static_cast<float>(vol));
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeDriscord_audioGetPeerVolume(
    JNIEnv* env, jclass, jstring jPeer
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    const float v    = CORE().audio_peer_volume(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
    return v;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioSetPeerMuted(
    JNIEnv* env, jclass, jstring jPeer, jboolean muted
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().audio_set_peer_muted(peer, muted == JNI_TRUE);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeDriscord_audioGetPeerMuted(
    JNIEnv* env, jclass, jstring jPeer
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    const bool m     = CORE().audio_peer_muted(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
    return m ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioSetScreenAudioReceiver(
    JNIEnv* env, jclass, jstring jPeerId, jlong screenHandle
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().audio_set_screen_audio_receiver(peer, screenHandle != 0);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioUnsetScreenAudioReceiver(
    JNIEnv* env, jclass, jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().audio_unset_screen_audio_receiver(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioAddScreenAudioToMixer(
    JNIEnv* env, jclass, jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().audio_add_screen_audio_to_mixer(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioRemoveScreenAudioFromMixer(
    JNIEnv* env, jclass, jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().audio_remove_screen_audio_from_mixer(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

} // extern "C"
