#include "jni_common.hpp"
#include "audio/capture/system_audio_capture.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeScreenCapture_systemAudioAvailable(JNIEnv*, jclass) {
    return SystemAudioCapture::available() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_NativeScreenCapture_listTargets(JNIEnv* env, jclass) {
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
    return env->NewStringUTF(arr.dump().c_str());
}

JNIEXPORT jbyteArray JNICALL
Java_com_driscord_NativeScreenCapture_grabThumbnail(JNIEnv* env, jclass,
        jstring jTargetJson, jint maxW, jint maxH) {
    const char* raw = env->GetStringUTFChars(jTargetJson, nullptr);
    CaptureTarget target = target_from_json(json::parse(raw));
    env->ReleaseStringUTFChars(jTargetJson, raw);

    auto frame = ScreenCapture::grab_thumbnail(target,
        static_cast<int>(maxW), static_cast<int>(maxH));
    if (frame.data.empty()) return nullptr;

    // BGRA → RGBA swap
    for (size_t i = 0; i < frame.data.size(); i += 4)
        std::swap(frame.data[i], frame.data[i + 2]);

    jbyteArray out = env->NewByteArray(static_cast<jsize>(frame.data.size()));
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(frame.data.size()),
                            reinterpret_cast<const jbyte*>(frame.data.data()));
    return out;
}

} // extern "C"
