#pragma once

#include <jni.h>
#include <mutex>
#include <string>

// ---------------------------------------------------------------------------
// JNI callback helper
// ---------------------------------------------------------------------------

struct JniCallback {
    JavaVM*   jvm = nullptr;
    jobject   obj = nullptr;
    jmethodID mid = nullptr;

    void clear(JNIEnv* env) {
        if (obj) { env->DeleteGlobalRef(obj); obj = nullptr; }
        jvm = nullptr; mid = nullptr;
    }

    JNIEnv* attach() const {
        if (!jvm) return nullptr;
        JNIEnv* env = nullptr;
        if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
            jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr);
        return env;
    }
};

inline void set_callback(JNIEnv* env, JniCallback& cb, jobject listener,
                         const char* method, const char* sig) {
    if (cb.obj) { env->DeleteGlobalRef(cb.obj); cb.obj = nullptr; }
    if (!listener) return;
    env->GetJavaVM(&cb.jvm);
    cb.obj = env->NewGlobalRef(listener);
    cb.mid = env->GetMethodID(env->GetObjectClass(listener), method, sig);
}

inline void fire_string(JniCallback& cb, std::mutex& mtx, const std::string& s) {
    std::scoped_lock lk(mtx);
    if (!cb.obj) return;
    auto* env = cb.attach();
    if (!env) return;
    jstring js = env->NewStringUTF(s.c_str());
    env->CallVoidMethod(cb.obj, cb.mid, js);
    env->DeleteLocalRef(js);
}
