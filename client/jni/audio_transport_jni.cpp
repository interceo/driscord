#include "audio_transport_jni.hpp"
#include "audio_receiver_jni.hpp"
#include "screen_session_jni.hpp"

#define AUDIO_TRANSPORT(h) reinterpret_cast<AudioTransportJni*>(h)
#define AUDIO_RECEIVER(h)  reinterpret_cast<AudioReceiverJni*>(h)
#define SCREEN_SESSION(h)  reinterpret_cast<ScreenSessionJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeAudioTransport_create(JNIEnv*, jclass, jlong transportHandle) {
    return reinterpret_cast<jlong>(
        new AudioTransportJni(*reinterpret_cast<TransportJni*>(transportHandle)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_destroy(JNIEnv*, jclass, jlong h) {
    delete AUDIO_TRANSPORT(h);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_sendAudio(JNIEnv* env, jclass, jlong h,
        jbyteArray jdata, jint len) {
    auto* buf = env->GetByteArrayElements(jdata, nullptr);
    AUDIO_TRANSPORT(h)->channel.send_audio(reinterpret_cast<const uint8_t*>(buf), len);
    env->ReleaseByteArrayElements(jdata, buf, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_registerVoiceReceiver(JNIEnv* env, jclass, jlong h,
        jstring jPeer, jlong receiverHandle) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    AUDIO_TRANSPORT(h)->register_voice(peer, &AUDIO_RECEIVER(receiverHandle)->receiver);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_unregisterVoiceReceiver(JNIEnv* env, jclass, jlong h,
        jstring jPeer) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    AUDIO_TRANSPORT(h)->unregister_voice(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_setScreenAudioReceiver(JNIEnv*, jclass,
        jlong audioHandle, jlong screenHandle) {
    AudioReceiver* recv = nullptr;
    if (screenHandle != 0)
        recv = SCREEN_SESSION(screenHandle)->session.audio_receiver();
    AUDIO_TRANSPORT(audioHandle)->set_screen_audio_recv(recv);
}

} // extern "C"
