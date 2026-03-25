#include <jni.h>
#include <nlohmann/json.hpp>

#include "video/capture/screen_capture.hpp"
#include "audio/capture/system_audio_capture.hpp"

using json = nlohmann::json;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeScreenCapture_systemAudioAvailable(JNIEnv*, jclass) {
    return SystemAudioCapture::available() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeScreenCapture_listTargets(JNIEnv* env, jclass) {
    auto targets = ScreenCapture::list_targets();
    json arr = json::array();
    for (auto& t : targets) {
        arr.push_back({
            {"type",   t.type == CaptureTarget::Monitor ? 0 : 1},
            {"id",     t.id},    {"name",   t.name},
            {"width",  t.width}, {"height", t.height},
            {"x",      t.x},     {"y",      t.y}
        });
    }
    return env->NewStringUTF(arr.dump(-1, ' ', /*ensure_ascii=*/true,
        nlohmann::json::error_handler_t::replace).c_str());
}

JNIEXPORT jbyteArray JNICALL
Java_com_driscord_jni_NativeScreenCapture_grabThumbnail(JNIEnv* env, jclass,
        jstring jTargetJson, jint maxW, jint maxH) {
    const char* raw = env->GetStringUTFChars(jTargetJson, nullptr);
    auto target = CaptureTarget::from_json(json::parse(raw));
    env->ReleaseStringUTFChars(jTargetJson, raw);

    auto frame = ScreenCapture::grab_thumbnail(target,
        static_cast<int>(maxW), static_cast<int>(maxH));
    if (frame.data.empty()) return nullptr;

    auto rgba = frame.to_rgba();
    jbyteArray out = env->NewByteArray(static_cast<jsize>(rgba.size()));
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(rgba.size()),
                            reinterpret_cast<const jbyte*>(rgba.data()));
    return out;
}

} // extern "C"
