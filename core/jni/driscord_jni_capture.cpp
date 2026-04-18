#include "driscord_state.hpp"
#include "jni_common.hpp"

#include "audio/capture/system_audio_capture.hpp"
#include "utils/byte_utils.hpp"

#define CORE() DriscordState::get().core

static constexpr int kThumbnailHeaderSize = 8; // width_le32 + height_le32

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_captureSystemAudioAvailable(JNIEnv*,
    jclass)
{
    return SystemAudioCapture::available() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeDriscord_captureVideoListTargets(JNIEnv* env,
    jclass)
{
    return jni_utf8_to_jstring(env, CORE().capture_video_list_targets_json());
}

JNIEXPORT jbyteArray JNICALL
Java_com_driscord_jni_NativeDriscord_captureGrabThumbnail(JNIEnv* env,
    jclass,
    jstring jTargetJson,
    jint maxW,
    jint maxH)
{
    auto targetJson = jni_jstring_to_utf8(env, jTargetJson);
    auto thumb = CORE().capture_grab_thumbnail(targetJson,
        static_cast<int>(maxW), static_cast<int>(maxH));

    if (thumb.rgba.empty()) {
        return nullptr;
    }

    auto totalSize = static_cast<jsize>(kThumbnailHeaderSize + thumb.rgba.size());
    jbyteArray out = env->NewByteArray(totalSize);

    uint8_t header[kThumbnailHeaderSize];
    utils::write_u32_le(header, static_cast<uint32_t>(thumb.width));
    utils::write_u32_le(header + 4, static_cast<uint32_t>(thumb.height));

    env->SetByteArrayRegion(out, 0, kThumbnailHeaderSize,
        reinterpret_cast<const jbyte*>(header));
    env->SetByteArrayRegion(out, kThumbnailHeaderSize,
        static_cast<jsize>(thumb.rgba.size()),
        reinterpret_cast<const jbyte*>(thumb.rgba.data()));
    return out;
}

} // extern "C"
