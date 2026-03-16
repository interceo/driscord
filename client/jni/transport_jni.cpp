#include "transport_jni.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define TRANSPORT(h) reinterpret_cast<TransportJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeTransport_create(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new TransportJni());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_destroy(JNIEnv*, jclass, jlong h) {
    delete TRANSPORT(h);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_addTurnServer(JNIEnv* env, jclass, jlong h,
        jstring jUrl, jstring jUser, jstring jPass) {
    auto url  = env->GetStringUTFChars(jUrl,  nullptr);
    auto user = env->GetStringUTFChars(jUser, nullptr);
    auto pass = env->GetStringUTFChars(jPass, nullptr);
    TRANSPORT(h)->transport.add_turn_server(url, user, pass);
    env->ReleaseStringUTFChars(jUrl,  url);
    env->ReleaseStringUTFChars(jUser, user);
    env->ReleaseStringUTFChars(jPass, pass);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_connect(JNIEnv* env, jclass, jlong h, jstring jUrl) {
    auto url = env->GetStringUTFChars(jUrl, nullptr);
    TRANSPORT(h)->transport.connect(url);
    env->ReleaseStringUTFChars(jUrl, url);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_disconnect(JNIEnv*, jclass, jlong h) {
    TRANSPORT(h)->transport.disconnect();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeTransport_connected(JNIEnv*, jclass, jlong h) {
    return TRANSPORT(h)->transport.connected() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_NativeTransport_localId(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(TRANSPORT(h)->transport.local_id().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_driscord_NativeTransport_peers(JNIEnv* env, jclass, jlong h) {
    auto ps = TRANSPORT(h)->transport.peers();
    json arr = json::array();
    for (auto& p : ps) arr.push_back({{"id", p.id}, {"connected", p.primary_open}});
    return env->NewStringUTF(arr.dump().c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_setOnPeerJoined(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* t = TRANSPORT(h);
    std::scoped_lock lk(t->cb_mutex);
    set_callback(env, t->on_peer_joined, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_setOnPeerLeft(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* t = TRANSPORT(h);
    std::scoped_lock lk(t->cb_mutex);
    set_callback(env, t->on_peer_left, cb, "invoke", "(Ljava/lang/String;)V");
}

} // extern "C"
