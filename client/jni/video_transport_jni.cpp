#include "video_transport_jni.hpp"

#define VIDEO_TRANSPORT(h) reinterpret_cast<VideoTransportJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeVideoTransport_create(JNIEnv*, jclass, jlong transportHandle) {
    return reinterpret_cast<jlong>(
        new VideoTransportJni(*reinterpret_cast<TransportJni*>(transportHandle)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_destroy(JNIEnv*, jclass, jlong h) {
    delete VIDEO_TRANSPORT(h);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_setWatching(JNIEnv*, jclass, jlong h, jboolean watching) {
    VIDEO_TRANSPORT(h)->watching.store(watching == JNI_TRUE, std::memory_order_relaxed);
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeVideoTransport_watching(JNIEnv*, jclass, jlong h) {
    return VIDEO_TRANSPORT(h)->watching.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_removeStreamingPeer(JNIEnv* env, jclass, jlong h,
        jstring jPeer) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    VIDEO_TRANSPORT(h)->remove_streaming_peer(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_sendKeyframeRequest(JNIEnv*, jclass, jlong h) {
    VIDEO_TRANSPORT(h)->channel.send_keyframe_request();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_setOnNewStreamingPeer(JNIEnv* env, jclass, jlong h,
        jobject cb) {
    auto* vt = VIDEO_TRANSPORT(h);
    std::scoped_lock lk(vt->cb_mutex);
    set_callback(env, vt->on_streaming_peer, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_setOnStreamingPeerRemoved(JNIEnv* env, jclass, jlong h,
        jobject cb) {
    auto* vt = VIDEO_TRANSPORT(h);
    std::scoped_lock lk(vt->cb_mutex);
    set_callback(env, vt->on_streaming_peer_removed, cb, "invoke", "(Ljava/lang/String;)V");
}

} // extern "C"
