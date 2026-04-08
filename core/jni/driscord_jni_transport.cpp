#include "driscord_state.hpp"
#include "jni_common.hpp"

#define CORE() DriscordState::get().core

extern "C" {

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_addTurnServer(JNIEnv* env, jclass,
        jstring jUrl, jstring jUser, jstring jPass) {
    auto url  = env->GetStringUTFChars(jUrl,  nullptr);
    auto user = env->GetStringUTFChars(jUser, nullptr);
    auto pass = env->GetStringUTFChars(jPass, nullptr);
    CORE().add_turn_server(url, user, pass);
    env->ReleaseStringUTFChars(jUrl,  url);
    env->ReleaseStringUTFChars(jUser, user);
    env->ReleaseStringUTFChars(jPass, pass);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_connect(JNIEnv* env, jclass, jstring jUrl) {
    auto url = env->GetStringUTFChars(jUrl, nullptr);
    CORE().connect(url);
    env->ReleaseStringUTFChars(jUrl, url);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_disconnect(JNIEnv*, jclass) {
    CORE().disconnect();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_connected(JNIEnv*, jclass) {
    return CORE().connected() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_localId(JNIEnv* env, jclass) {
    return env->NewStringUTF(CORE().local_id().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_peers(JNIEnv* env, jclass) {
    return env->NewStringUTF(CORE().peers_json().c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_setOnPeerJoined(JNIEnv* env, jclass, jobject cb) {
    CORE().set_on_peer_joined(make_string_cb(env, cb));
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_setOnPeerLeft(JNIEnv* env, jclass, jobject cb) {
    CORE().set_on_peer_left(make_string_cb(env, cb));
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_setOnStreamingStarted(JNIEnv* env, jclass, jobject cb) {
    CORE().set_on_streaming_started(make_string_cb(env, cb));
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_setOnStreamingStopped(JNIEnv* env, jclass, jobject cb) {
    CORE().set_on_streaming_stopped(make_string_cb(env, cb));
}

} // extern "C"
