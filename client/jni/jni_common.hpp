#pragma once

#include <jni.h>
#include <functional>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// RAII wrapper for a JNI global ref + method ID.
// Use via shared_ptr — destructor cleans up the global ref.
// ---------------------------------------------------------------------------

struct JniRef {
    JavaVM*   jvm = nullptr;
    jobject   obj = nullptr;
    jmethodID mid = nullptr;

    JniRef() = default;

    JniRef(JNIEnv* env, jobject listener, const char* method, const char* sig) {
        if (!listener) return;
        env->GetJavaVM(&jvm);
        obj = env->NewGlobalRef(listener);
        mid = env->GetMethodID(env->GetObjectClass(listener), method, sig);
    }

    ~JniRef() {
        if (!obj || !jvm) return;
        JNIEnv* env = nullptr;
        if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
            env->DeleteGlobalRef(obj);
        }
    }

    JniRef(const JniRef&) = delete;
    JniRef& operator=(const JniRef&) = delete;

    JNIEnv* attach() const {
        if (!jvm) return nullptr;
        JNIEnv* env = nullptr;
        if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
            jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr);
        return env;
    }
};

// ---------------------------------------------------------------------------
// Helper: wrap a JNI StringCallback into a std::function<void(string)>.
// The shared_ptr ensures the global ref is released when the function is
// replaced or destroyed.
// ---------------------------------------------------------------------------

inline std::function<void(const std::string&)>
make_string_cb(JNIEnv* env, jobject listener) {
    if (!listener) return nullptr;
    auto ref = std::make_shared<JniRef>(env, listener, "invoke", "(Ljava/lang/String;)V");
    return [ref](const std::string& s) {
        auto* e = ref->attach();
        if (!e) return;
        jstring js = e->NewStringUTF(s.c_str());
        e->CallVoidMethod(ref->obj, ref->mid, js);
        e->DeleteLocalRef(js);
    };
}
