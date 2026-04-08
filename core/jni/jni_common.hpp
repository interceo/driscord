#pragma once

#include <functional>
#include <jni.h>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// RAII wrapper for a JNI global ref + method ID.
// Use via shared_ptr — destructor cleans up the global ref.
// ---------------------------------------------------------------------------

struct JniRef {
    JavaVM* jvm   = nullptr;
    jobject obj   = nullptr;
    jmethodID mid = nullptr;

    JniRef() = default;

    JniRef(JNIEnv* env, jobject listener, const char* method, const char* sig) {
        if (!listener) {
            return;
        }
        env->GetJavaVM(&jvm);
        obj = env->NewGlobalRef(listener);
        mid = env->GetMethodID(env->GetObjectClass(listener), method, sig);
    }

    ~JniRef() {
        if (!obj || !jvm) {
            return;
        }
        JNIEnv* env = nullptr;
        if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
            env->DeleteGlobalRef(obj);
        }
    }

    JniRef(const JniRef&)            = delete;
    JniRef& operator=(const JniRef&) = delete;

    JNIEnv* attach() const {
        if (!jvm) {
            return nullptr;
        }
        JNIEnv* env = nullptr;
        if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
            jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr);
        }
        return env;
    }
};

// ---------------------------------------------------------------------------
// Convert a UTF-8 std::string to a jstring via UTF-16.
// JNI's NewStringUTF() uses "Modified UTF-8" which mishandles 4-byte UTF-8
// (emoji, some CJK). This converts to UTF-16 and uses NewString() instead.
// ---------------------------------------------------------------------------

inline jstring jni_utf8_to_jstring(JNIEnv* env, const std::string& utf8) {
    std::vector<jchar> utf16;
    utf16.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = 0;
        auto c      = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c >> 5) == 0x06) {
            if (i + 1 >= utf8.size()) {
                break;
            }
            cp = (c & 0x1F) << 6 | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            i += 2;
        } else if ((c >> 4) == 0x0E) {
            if (i + 2 >= utf8.size()) {
                break;
            }
            cp = (c & 0x0F) << 12 | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 6 |
                 (static_cast<unsigned char>(utf8[i + 2]) & 0x3F);
            i += 3;
        } else if ((c >> 3) == 0x1E) {
            if (i + 3 >= utf8.size()) {
                break;
            }
            cp = (c & 0x07) << 18 | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 12 |
                 (static_cast<unsigned char>(utf8[i + 2]) & 0x3F) << 6 |
                 (static_cast<unsigned char>(utf8[i + 3]) & 0x3F);
            i += 4;
        } else {
            utf16.push_back(0xFFFD);
            i += 1;
            continue;
        }
        if (cp <= 0xFFFF) {
            utf16.push_back(static_cast<jchar>(cp));
        } else {
            cp -= 0x10000;
            utf16.push_back(static_cast<jchar>(0xD800 + (cp >> 10)));
            utf16.push_back(static_cast<jchar>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return env->NewString(utf16.data(), static_cast<jsize>(utf16.size()));
}

// ---------------------------------------------------------------------------
// Convert a jstring to a UTF-8 std::string via UTF-16.
// GetStringUTFChars() returns "Modified UTF-8" which encodes supplementary
// code points (> U+FFFF) as two 3-byte surrogate sequences — invalid standard
// UTF-8. Using GetStringChars() (UTF-16) and re-encoding avoids this.
// ---------------------------------------------------------------------------

inline std::string jni_jstring_to_utf8(JNIEnv* env, jstring js) {
    if (!js) {
        return {};
    }
    const jchar* chars = env->GetStringChars(js, nullptr);
    jsize len          = env->GetStringLength(js);

    std::string utf8;
    utf8.reserve(static_cast<size_t>(len) * 3);

    for (jsize i = 0; i < len;) {
        uint32_t cp;
        jchar c = chars[i++];
        if (c >= 0xD800 && c <= 0xDBFF && i < len) {
            jchar c2 = chars[i];
            if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                cp = 0x10000u + ((static_cast<uint32_t>(c) - 0xD800u) << 10) +
                     (static_cast<uint32_t>(c2) - 0xDC00u);
                ++i;
            } else {
                cp = 0xFFFD;
            }
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            cp = 0xFFFD;
        } else {
            cp = c;
        }

        if (cp < 0x80) {
            utf8 += static_cast<char>(cp);
        } else if (cp < 0x800) {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            utf8 += static_cast<char>(0xF0 | (cp >> 18));
            utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    env->ReleaseStringChars(js, chars);
    return utf8;
}

// ---------------------------------------------------------------------------
// Helper: wrap a JNI StringCallback into a std::function<void(string)>.
// The shared_ptr ensures the global ref is released when the function is
// replaced or destroyed.
// ---------------------------------------------------------------------------

inline std::function<void(const std::string&)> make_string_cb(JNIEnv* env, jobject listener) {
    if (!listener) {
        return nullptr;
    }
    auto ref = std::make_shared<JniRef>(env, listener, "invoke", "(Ljava/lang/String;)V");
    return [ref](const std::string& s) {
        auto* e = ref->attach();
        if (!e) {
            return;
        }
        jstring js = jni_utf8_to_jstring(e, s);
        e->CallVoidMethod(ref->obj, ref->mid, js);
        e->DeleteLocalRef(js);
    };
}
