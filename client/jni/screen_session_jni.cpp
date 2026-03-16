#include "screen_session_jni.hpp"
#include "audio_receiver_jni.hpp"
#include "audio/audio_mixer.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define SCREEN_SESSION(h) reinterpret_cast<ScreenSessionJni*>(h)
#define AUDIO_MIXER(h)    reinterpret_cast<AudioMixer*>(h)

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeScreenSession_create(JNIEnv*, jclass,
        jint bufMs, jint maxSyncMs,
        jlong videoTransportHandle, jlong audioTransportHandle) {
    return reinterpret_cast<jlong>(new ScreenSessionJni(
        static_cast<int>(bufMs), static_cast<int>(maxSyncMs),
        reinterpret_cast<VideoTransportJni*>(videoTransportHandle),
        reinterpret_cast<AudioTransportJni*>(audioTransportHandle)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_destroy(JNIEnv*, jclass, jlong h) {
    auto* s = SCREEN_SESSION(h);
    s->video_transport->clear_video_sink();
    delete s;
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeScreenSession_startSharing(JNIEnv* env, jclass, jlong h,
        jstring jTargetJson, jint maxW, jint maxH, jint fps, jint bitrateKbps,
        jboolean shareAudio) {
    const char* raw = env->GetStringUTFChars(jTargetJson, nullptr);
    CaptureTarget target = target_from_json(json::parse(raw));
    env->ReleaseStringUTFChars(jTargetJson, raw);
    bool ok = SCREEN_SESSION(h)->start_sharing(
        target, static_cast<int>(maxW), static_cast<int>(maxH),
        static_cast<int>(fps), static_cast<int>(bitrateKbps), shareAudio == JNI_TRUE);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_stopSharing(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->session.stop_sharing();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeScreenSession_sharing(JNIEnv*, jclass, jlong h) {
    return SCREEN_SESSION(h)->session.sharing() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeScreenSession_sharingAudio(JNIEnv*, jclass, jlong h) {
    return SCREEN_SESSION(h)->session.sharing_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_forceKeyframe(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->session.force_keyframe();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_update(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->update();
}

JNIEXPORT jstring JNICALL
Java_com_driscord_NativeScreenSession_activePeer(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(SCREEN_SESSION(h)->session.active_peer().c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeScreenSession_active(JNIEnv*, jclass, jlong h) {
    return SCREEN_SESSION(h)->session.active() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_reset(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->session.reset();
    SCREEN_SESSION(h)->last_peer.clear();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_addAudioReceiverToMixer(JNIEnv*, jclass,
        jlong screenHandle, jlong mixerHandle) {
    AUDIO_MIXER(mixerHandle)->add_source(
        SCREEN_SESSION(screenHandle)->session.audio_receiver());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_removeAudioReceiverFromMixer(JNIEnv*, jclass,
        jlong screenHandle, jlong mixerHandle) {
    AUDIO_MIXER(mixerHandle)->remove_source(
        SCREEN_SESSION(screenHandle)->session.audio_receiver());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_resetAudioReceiver(JNIEnv*, jclass, jlong h) {
    SCREEN_SESSION(h)->session.audio_receiver()->reset();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_setStreamVolume(JNIEnv*, jclass, jlong h, jfloat vol) {
    SCREEN_SESSION(h)->session.audio_receiver()->set_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeScreenSession_streamVolume(JNIEnv*, jclass, jlong h) {
    return SCREEN_SESSION(h)->session.audio_receiver()->volume();
}

JNIEXPORT jstring JNICALL
Java_com_driscord_NativeScreenSession_stats(JNIEnv* env, jclass, jlong h) {
    auto* s = SCREEN_SESSION(h);
    auto vs = s->session.video_stats();
    auto as = s->session.audio_stats();
    json j = {
        {"width",  s->last_w}, {"height", s->last_h},
        {"measuredKbps", s->session.measured_kbps()},
        {"video", {{"queue", vs.queue_size}, {"bufMs", vs.buffered_ms},
                   {"drops", vs.drop_count}, {"misses", vs.miss_count}}},
        {"audio", {{"queue", as.queue_size}, {"bufMs", as.buffered_ms},
                   {"drops", as.drop_count}, {"misses", as.miss_count}}}
    };
    return env->NewStringUTF(j.dump().c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_setOnFrame(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* s = SCREEN_SESSION(h);
    std::scoped_lock lk(s->cb_mutex);
    set_callback(env, s->on_frame_cb, cb, "invoke", "(Ljava/lang/String;[BII)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_setOnFrameRemoved(JNIEnv* env, jclass, jlong h,
        jobject cb) {
    auto* s = SCREEN_SESSION(h);
    std::scoped_lock lk(s->cb_mutex);
    set_callback(env, s->on_frame_removed_cb, cb, "invoke", "(Ljava/lang/String;)V");
}

} // extern "C"
