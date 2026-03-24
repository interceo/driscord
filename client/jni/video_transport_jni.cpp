#include "video_transport_jni.hpp"
#include "driscord_state.hpp"

VideoTransportJni::VideoTransportJni(TransportJni& t) : channel(t.transport) {
    channel.on_new_streaming_peer([this](const std::string& peer_id) {
        fire_string(on_streaming_peer, cb_mutex, peer_id);
    });
    channel.on_streaming_peer_removed([this](const std::string& peer_id) {
        fire_string(on_streaming_peer_removed, cb_mutex, peer_id);
    });
}

#define VT() DriscordState::get().video_transport

extern "C" {

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_setWatching(JNIEnv*, jclass, jboolean watching) {
    if (watching == JNI_FALSE) {
        VT().channel.clear_watched_peers();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeVideoTransport_watching(JNIEnv*, jclass) {
    return VT().channel.watching() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_removeStreamingPeer(
    JNIEnv* env, jclass, jstring jPeer
) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    VT().channel.remove_streaming_peer(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_sendKeyframeRequest(JNIEnv*, jclass) {
    VT().channel.send_keyframe_request();
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_setOnNewStreamingPeer(
    JNIEnv* env, jclass, jobject cb
) {
    auto& vt = VT();
    std::scoped_lock lk(vt.cb_mutex);
    set_callback(env, vt.on_streaming_peer, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_setOnStreamingPeerRemoved(
    JNIEnv* env, jclass, jobject cb
) {
    auto& vt = VT();
    std::scoped_lock lk(vt.cb_mutex);
    set_callback(env, vt.on_streaming_peer_removed, cb, "invoke", "(Ljava/lang/String;)V");
}

}  // extern "C"
