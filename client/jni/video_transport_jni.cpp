#include "driscord_state.hpp"

#define CORE()  DriscordState::get().core
#define STATE() DriscordState::get()

extern "C" {

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_setWatching(JNIEnv*, jclass, jboolean watching) {
    if (watching == JNI_FALSE) {
        CORE().video_transport.clear_watched_peers();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeVideoTransport_watching(JNIEnv*, jclass) {
    return CORE().video_transport.watching() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_removeStreamingPeer(
    JNIEnv* env, jclass, jstring jPeer
) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().video_transport.remove_streaming_peer(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_sendKeyframeRequest(JNIEnv*, jclass) {
    CORE().video_transport.send_keyframe_request();
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_setOnNewStreamingPeer(
    JNIEnv* env, jclass, jobject cb
) {
    auto& s = STATE();
    std::scoped_lock lk(s.video_mtx);
    set_callback(env, s.on_streaming_peer, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeVideoTransport_setOnStreamingPeerRemoved(
    JNIEnv* env, jclass, jobject cb
) {
    auto& s = STATE();
    std::scoped_lock lk(s.video_mtx);
    set_callback(env, s.on_streaming_peer_removed, cb, "invoke", "(Ljava/lang/String;)V");
}

}  // extern "C"
