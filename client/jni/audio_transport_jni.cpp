#include "audio_transport_jni.hpp"
#include "driscord_state.hpp"
#include "screen_session_jni.hpp"

#define SCREEN_SESSION(h) reinterpret_cast<ScreenSessionJni*>(h)

AudioTransportJni::AudioTransportJni(TransportJni& t)
    : channel(t.transport) {}

#define AT() DriscordState::get().audio_transport.channel

extern "C" {

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_sendAudio(
    JNIEnv* env,
    jclass,
    jbyteArray jdata,
    jint len
) {
    auto* buf = env->GetByteArrayElements(jdata, nullptr);
    AT().send_audio(reinterpret_cast<const uint8_t*>(buf), len);
    env->ReleaseByteArrayElements(jdata, buf, JNI_ABORT);
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeAudioTransport_start(JNIEnv*, jclass) {
    return AT().start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_stop(JNIEnv*, jclass) {
    AT().stop();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeAudioTransport_deafened(JNIEnv*, jclass) {
    return AT().deafened() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_setDeafened(
    JNIEnv*, jclass, jboolean deaf
) {
    AT().set_deafened(deaf == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeAudioTransport_masterVolume(
    JNIEnv*, jclass
) {
    return AT().master_volume();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_setMasterVolume(
    JNIEnv*, jclass, jfloat vol
) {
    AT().set_master_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeAudioTransport_outputLevel(
    JNIEnv*, jclass
) {
    return AT().output_level();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeAudioTransport_selfMuted(
    JNIEnv*, jclass
) {
    return AT().self_muted() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_setSelfMuted(
    JNIEnv*, jclass, jboolean muted
) {
    AT().set_self_muted(muted == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeAudioTransport_inputLevel(
    JNIEnv*, jclass
) {
    return AT().input_level();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_onPeerJoined(
    JNIEnv* env,
    jclass,
    jstring jPeer,
    jint jitterMs
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    AT().on_peer_joined(peer, static_cast<int>(jitterMs));
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_onPeerLeft(
    JNIEnv* env,
    jclass,
    jstring jPeer
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    AT().on_peer_left(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_setPeerVolume(
    JNIEnv* env,
    jclass,
    jstring jPeer,
    jfloat vol
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    AT().set_peer_volume(peer, static_cast<float>(vol));
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeAudioTransport_getPeerVolume(
    JNIEnv* env,
    jclass,
    jstring jPeer
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    const float v    = AT().peer_volume(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
    return v;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_setPeerMuted(
    JNIEnv* env,
    jclass,
    jstring jPeer,
    jboolean muted
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    AT().set_peer_muted(peer, muted == JNI_TRUE);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeAudioTransport_getPeerMuted(
    JNIEnv* env,
    jclass,
    jstring jPeer
) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    const bool m     = AT().peer_muted(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
    return m ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_setScreenAudioReceiver(
    JNIEnv* env,
    jclass,
    jstring jPeerId,
    jlong screenHandle
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    std::shared_ptr<AudioReceiver> recv;
    if (screenHandle != 0) {
        recv = SCREEN_SESSION(screenHandle)->session.audio_receiver();
    }
    AT().set_screen_audio_recv(peer, std::move(recv));
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_unsetScreenAudioReceiver(
    JNIEnv* env,
    jclass,
    jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    AT().unset_screen_audio_recv(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_addScreenAudioToMixer(
    JNIEnv* env,
    jclass,
    jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    AT().add_screen_audio_to_mixer(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_removeScreenAudioFromMixer(
    JNIEnv* env,
    jclass,
    jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    AT().remove_screen_audio_from_mixer(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

} // extern "C"
