#include "driscord_state.hpp"
#include "jni_common.hpp"

#define CORE() DriscordState::get().core

extern "C" {

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioSend(JNIEnv* env,
    jclass,
    jbyteArray jdata,
    jint len)
{
    auto* buf = env->GetByteArrayElements(jdata, nullptr);
    CORE().audio_transport.send_audio(reinterpret_cast<const uint8_t*>(buf), len);
    env->ReleaseByteArrayElements(jdata, buf, JNI_ABORT);
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_audioStart(JNIEnv* env, jclass)
{
    auto r = CORE().audio_transport.start();
    return r ? nullptr : env->NewStringUTF(to_string(r.error()));
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeDriscord_audioStop(JNIEnv*,
    jclass)
{
    CORE().audio_transport.stop();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_audioDeafened(JNIEnv*, jclass)
{
    return CORE().audio_transport.deafened() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioSetDeafened(JNIEnv*,
    jclass,
    jboolean deaf)
{
    CORE().audio_transport.set_deafened(deaf == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_jni_NativeDriscord_audioMasterVolume(JNIEnv*, jclass)
{
    return CORE().audio_transport.master_volume();
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioSetMasterVolume(JNIEnv*,
    jclass,
    jfloat vol)
{
    CORE().audio_transport.set_master_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_jni_NativeDriscord_audioOutputLevel(JNIEnv*, jclass)
{
    return CORE().audio_transport.output_level();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_audioSelfMuted(JNIEnv*, jclass)
{
    return CORE().audio_transport.self_muted() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioSetSelfMuted(JNIEnv*,
    jclass,
    jboolean muted)
{
    CORE().audio_transport.set_self_muted(muted == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_jni_NativeDriscord_audioInputLevel(JNIEnv*, jclass)
{
    return CORE().audio_transport.input_level();
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_audioListInputDevices(JNIEnv* env,
    jclass)
{
    const std::string json = AudioTransport::list_input_devices_json();
    return env->NewStringUTF(json.c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioSetInputDevice(JNIEnv* env,
    jclass,
    jstring jId)
{
    const char* id = env->GetStringUTFChars(jId, nullptr);
    CORE().audio_transport.set_input_device(id);
    env->ReleaseStringUTFChars(jId, id);
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_audioListOutputDevices(JNIEnv* env,
    jclass)
{
    const std::string json = AudioTransport::list_output_devices_json();
    return env->NewStringUTF(json.c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioSetOutputDevice(JNIEnv* env,
    jclass,
    jstring jId)
{
    const char* id = env->GetStringUTFChars(jId, nullptr);
    CORE().audio_transport.set_output_device(id);
    env->ReleaseStringUTFChars(jId, id);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioOnPeerJoined(JNIEnv* env,
    jclass,
    jstring jPeer,
    jint jitterMs)
{
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().audio_transport.on_peer_joined(peer, static_cast<int>(jitterMs));
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioOnPeerLeft(JNIEnv* env,
    jclass,
    jstring jPeer)
{
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().audio_transport.on_peer_left(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioSetPeerVolume(JNIEnv* env,
    jclass,
    jstring jPeer,
    jfloat vol)
{
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().audio_transport.set_peer_volume(peer, static_cast<float>(vol));
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_jni_NativeDriscord_audioGetPeerVolume(JNIEnv* env,
    jclass,
    jstring jPeer)
{
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    const float v = CORE().audio_transport.peer_volume(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
    return v;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioSetPeerMuted(JNIEnv* env,
    jclass,
    jstring jPeer,
    jboolean muted)
{
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().audio_transport.set_peer_muted(peer, muted == JNI_TRUE);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_audioGetPeerMuted(JNIEnv* env,
    jclass,
    jstring jPeer)
{
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    const bool m = CORE().audio_transport.peer_muted(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
    return m ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioSetScreenAudioReceiver(
    JNIEnv* env,
    jclass,
    jstring jPeerId,
    jlong screenHandle)
{
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().audio_set_screen_audio_receiver(peer, screenHandle != 0);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioUnsetScreenAudioReceiver(
    JNIEnv* env,
    jclass,
    jstring jPeerId)
{
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().audio_transport.unset_screen_audio_recv(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioAddScreenAudioToMixer(
    JNIEnv* env,
    jclass,
    jstring jPeerId)
{
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().audio_transport.add_screen_audio_to_mixer(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_audioRemoveScreenAudioFromMixer(
    JNIEnv* env,
    jclass,
    jstring jPeerId)
{
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    CORE().audio_transport.remove_screen_audio_from_mixer(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

} // extern "C"
