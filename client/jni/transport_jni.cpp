#include "transport_jni.hpp"
#include "driscord_state.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

TransportJni::TransportJni(Transport& transport) {
    transport.on_peer_joined([this](const std::string& id) {
        fire_string(on_peer_joined, cb_mutex, id);
    });
    transport.on_peer_left([this](const std::string& id) {
        fire_string(on_peer_left, cb_mutex, id);
    });
}

#define CORE() DriscordState::get().core
#define T_CBS() DriscordState::get().transport_cbs

extern "C" {

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_addTurnServer(JNIEnv* env, jclass,
        jstring jUrl, jstring jUser, jstring jPass) {
    auto url  = env->GetStringUTFChars(jUrl,  nullptr);
    auto user = env->GetStringUTFChars(jUser, nullptr);
    auto pass = env->GetStringUTFChars(jPass, nullptr);
    CORE().transport.add_turn_server(url, user, pass);
    env->ReleaseStringUTFChars(jUrl,  url);
    env->ReleaseStringUTFChars(jUser, user);
    env->ReleaseStringUTFChars(jPass, pass);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_connect(JNIEnv* env, jclass, jstring jUrl) {
    auto url = env->GetStringUTFChars(jUrl, nullptr);
    CORE().transport.connect(url);
    env->ReleaseStringUTFChars(jUrl, url);
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_disconnect(JNIEnv*, jclass) {
    CORE().transport.disconnect();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeTransport_connected(JNIEnv*, jclass) {
    return CORE().transport.connected() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeTransport_localId(JNIEnv* env, jclass) {
    return env->NewStringUTF(CORE().transport.local_id().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeTransport_peers(JNIEnv* env, jclass) {
    auto ps = CORE().transport.peers();
    json arr = json::array();
    for (auto& p : ps) arr.push_back({{"id", p.id}, {"connected", p.primary_open}});
    return env->NewStringUTF(arr.dump().c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_setOnPeerJoined(JNIEnv* env, jclass, jobject cb) {
    auto& t = T_CBS();
    std::scoped_lock lk(t.cb_mutex);
    set_callback(env, t.on_peer_joined, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeTransport_setOnPeerLeft(JNIEnv* env, jclass, jobject cb) {
    auto& t = T_CBS();
    std::scoped_lock lk(t.cb_mutex);
    set_callback(env, t.on_peer_left, cb, "invoke", "(Ljava/lang/String;)V");
}

} // extern "C"
