#include "audio_transport_jni.hpp"
#include "audio_receiver_jni.hpp"
#include "screen_session_jni.hpp"


AudioTransportJni::AudioTransportJni(TransportJni& t) : channel(t.transport) {
    channel.on_audio_received([this](const std::string& peer_id,
                                     const uint8_t* data, size_t len) {
        std::scoped_lock lk(recv_mutex);
        auto it = voice_recv.find(peer_id);
        if (it != voice_recv.end()) it->second->push_packet(data, len);
    });
    channel.on_screen_audio_received([this](const std::string&,
                                             const uint8_t* data, size_t len) {
        std::scoped_lock lk(recv_mutex);
        if (screen_audio_recv) screen_audio_recv->push_packet(data, len);
    });
}

void AudioTransportJni::register_voice(const std::string& peer_id, AudioReceiver* recv) {
    std::scoped_lock lk(recv_mutex);
    voice_recv[peer_id] = recv;
}
void AudioTransportJni::unregister_voice(const std::string& peer_id) {
    std::scoped_lock lk(recv_mutex);
    voice_recv.erase(peer_id);
}
void AudioTransportJni::set_screen_audio_recv(AudioReceiver* recv) {
    std::scoped_lock lk(recv_mutex);
    screen_audio_recv = recv;
}

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
