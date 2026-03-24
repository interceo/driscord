#include "driscord_state.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define T() DriscordState::get().transport

extern "C" {

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_addTurnServer(JNIEnv* env, jclass,
        jstring jUrl, jstring jUser, jstring jPass) {
    auto url  = env->GetStringUTFChars(jUrl,  nullptr);
    auto user = env->GetStringUTFChars(jUser, nullptr);
    auto pass = env->GetStringUTFChars(jPass, nullptr);
    T().transport.add_turn_server(url, user, pass);
    env->ReleaseStringUTFChars(jUrl,  url);
    env->ReleaseStringUTFChars(jUser, user);
    env->ReleaseStringUTFChars(jPass, pass);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_connect(JNIEnv* env, jclass, jstring jUrl) {
    auto url = env->GetStringUTFChars(jUrl, nullptr);
    T().transport.connect(url);
    env->ReleaseStringUTFChars(jUrl, url);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_disconnect(JNIEnv*, jclass) {
    T().transport.disconnect();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeTransport_connected(JNIEnv*, jclass) {
    return T().transport.connected() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeTransport_localId(JNIEnv* env, jclass) {
    return env->NewStringUTF(T().transport.local_id().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeTransport_peers(JNIEnv* env, jclass) {
    auto ps = T().transport.peers();
    json arr = json::array();
    for (auto& p : ps) arr.push_back({{"id", p.id}, {"connected", p.primary_open}});
    return env->NewStringUTF(arr.dump().c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_setOnPeerJoined(JNIEnv* env, jclass, jobject cb) {
    auto& t = T();
    std::scoped_lock lk(t.cb_mutex);
    set_callback(env, t.on_peer_joined, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_setOnPeerLeft(JNIEnv* env, jclass, jobject cb) {
    auto& t = T();
    std::scoped_lock lk(t.cb_mutex);
    set_callback(env, t.on_peer_left, cb, "invoke", "(Ljava/lang/String;)V");
}

} // extern "C"
