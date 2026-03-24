#include "audio_transport_jni.hpp"
#include "audio_jni.hpp"
#include "driscord_state.hpp"
#include "screen_session_jni.hpp"

AudioTransportJni::AudioTransportJni(TransportJni& t)
    : channel(t.transport) {}

#define AT() DriscordState::get().audio_transport
#define SCREEN_SESSION(h) reinterpret_cast<ScreenSessionJni*>(h)

extern "C" {

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_sendAudio(
    JNIEnv* env,
    jclass,
    jbyteArray jdata,
    jint len
) {
    auto* buf = env->GetByteArrayElements(jdata, nullptr);
    AT().channel.send_audio(reinterpret_cast<const uint8_t*>(buf), len);
    env->ReleaseByteArrayElements(jdata, buf, JNI_ABORT);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_registerVoiceReceiver(
    JNIEnv* env,
    jclass,
    jstring jPeer,
    jlong receiverHandle
) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    AT().channel.register_voice(
        peer,
        reinterpret_cast<AudioReceiverJni*>(receiverHandle)->receiver
    );
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_unregisterVoiceReceiver(
    JNIEnv* env,
    jclass,
    jstring jPeer
) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    AT().channel.unregister_voice(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
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
    AT().channel.set_screen_audio_recv(peer, std::move(recv));
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeAudioTransport_unsetScreenAudioReceiver(
    JNIEnv* env,
    jclass,
    jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    AT().channel.unset_screen_audio_recv(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

} // extern "C"
