#include "driscord_state.hpp"
#include "jni_common.hpp"

#define CORE() DriscordState::get().core

extern "C" {

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_videoSetWatching(JNIEnv*, jclass, jboolean watching) {
    CORE().video_set_watching(watching == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_videoWatching(JNIEnv*, jclass) {
    return CORE().video_watching() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_videoRemoveStreamingPeer(JNIEnv* env, jclass, jstring jPeer) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    CORE().video_remove_streaming_peer(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_videoSendKeyframeRequest(JNIEnv*, jclass) {
    CORE().video_send_keyframe_request();
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_setOnNewStreamingPeer(JNIEnv* env, jclass, jobject cb) {
    CORE().set_on_new_streaming_peer(make_string_cb(env, cb));
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_setOnStreamingPeerRemoved(JNIEnv* env, jclass, jobject cb) {
    CORE().set_on_streaming_peer_removed(make_string_cb(env, cb));
}

} // extern "C"
