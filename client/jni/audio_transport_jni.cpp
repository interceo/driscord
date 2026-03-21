#include "audio_transport_jni.hpp"
#include "audio_jni.hpp"
#include "screen_session_jni.hpp"

AudioTransportJni::AudioTransportJni(TransportJni& t)
    : channel(t.transport) {}

#define AUDIO_TRANSPORT(h) reinterpret_cast<AudioTransportJni*>(h)
#define SCREEN_SESSION(h) reinterpret_cast<ScreenSessionJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_driscord_jni_NativeAudioTransport_create(JNIEnv*, jclass, jlong transportHandle) {
    return reinterpret_cast<
        jlong>(new AudioTransportJni(*reinterpret_cast<TransportJni*>(transportHandle)));
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeAudioTransport_destroy(JNIEnv*, jclass, jlong h) {
    delete AUDIO_TRANSPORT(h);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_sendAudio(
    JNIEnv* env,
    jclass,
    jlong h,
    jbyteArray jdata,
    jint len
) {
    auto* buf = env->GetByteArrayElements(jdata, nullptr);
    AUDIO_TRANSPORT(h)->channel.send_audio(reinterpret_cast<const uint8_t*>(buf), len);
    env->ReleaseByteArrayElements(jdata, buf, JNI_ABORT);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_registerVoiceReceiver(
    JNIEnv* env,
    jclass,
    jlong h,
    jstring jPeer,
    jlong receiverHandle
) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    AUDIO_TRANSPORT(h)->channel.register_voice(
        peer,
        reinterpret_cast<AudioReceiverJni*>(receiverHandle)->receiver
    );
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_unregisterVoiceReceiver(
    JNIEnv* env,
    jclass,
    jlong h,
    jstring jPeer
) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    AUDIO_TRANSPORT(h)->channel.unregister_voice(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_setScreenAudioReceiver(
    JNIEnv* env,
    jclass,
    jlong audioHandle,
    jstring jPeerId,
    jlong screenHandle
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    std::shared_ptr<AudioReceiver> recv;
    if (screenHandle != 0) {
        recv = SCREEN_SESSION(screenHandle)->session.audio_receiver();
    }
    AUDIO_TRANSPORT(audioHandle)->channel.set_screen_audio_recv(peer, std::move(recv));
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_unsetScreenAudioReceiver(
    JNIEnv* env,
    jclass,
    jlong audioHandle,
    jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    AUDIO_TRANSPORT(audioHandle)->channel.unset_screen_audio_recv(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

} // extern "C"
