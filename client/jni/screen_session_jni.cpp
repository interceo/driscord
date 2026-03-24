#include "screen_session_jni.hpp"
#include "driscord_state.hpp"
#include "utils/vector_view.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

ScreenSessionJni::ScreenSessionJni(int buf_ms, int max_sync_ms)
    : session(
          buf_ms,
          std::chrono::milliseconds(max_sync_ms),
          [](const uint8_t* d, size_t l) {
              DriscordState::get().video_transport.channel.send_video(d, l);
          },
          []() {
              DriscordState::get().video_transport.channel.send_keyframe_request();
          },
          [](const uint8_t* d, size_t l) {
              DriscordState::get().audio_transport.channel.send_screen_audio(d, l);
          }
      ) {
    DriscordState::get().video_transport.channel.set_video_sink(
        [this](const std::string& peer_id, const uint8_t* data, size_t len) {
            session.push_video_packet(peer_id, utils::vector_view<const uint8_t>{data, len});
        },
        [this]() {
            if (session.sharing()) {
                session.force_keyframe();
            }
        }
    );

    session.set_on_frame([this](const std::string& peer_id, const uint8_t* rgba, int w, int h) {
        fire_frame(peer_id, rgba, w, h);
    });
    session.set_on_frame_removed([this](const std::string& peer_id) {
        fire_remove_frame(peer_id);
    });
}

void ScreenSessionJni::fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h) {
    std::scoped_lock lk(cb_mutex);
    if (!on_frame_cb.obj) {
        return;
    }
    auto* env = on_frame_cb.attach();
    if (!env) {
        return;
    }
    jstring jpeer    = env->NewStringUTF(peer_id.c_str());
    jbyteArray jdata = env->NewByteArray(w * h * 4);
    env->SetByteArrayRegion(jdata, 0, w * h * 4, reinterpret_cast<const jbyte*>(rgba));
    env->CallVoidMethod(on_frame_cb.obj, on_frame_cb.mid, jpeer, jdata, (jint)w, (jint)h);
    env->DeleteLocalRef(jdata);
    env->DeleteLocalRef(jpeer);
}

void ScreenSessionJni::fire_remove_frame(const std::string& peer_id) {
    DriscordState::get().video_transport.channel.remove_streaming_peer(peer_id);

    std::scoped_lock lk(cb_mutex);
    if (!on_frame_removed_cb.obj) {
        return;
    }
    auto* env = on_frame_removed_cb.attach();
    if (!env) {
        return;
    }
    jstring jpeer = env->NewStringUTF(peer_id.c_str());
    env->CallVoidMethod(on_frame_removed_cb.obj, on_frame_removed_cb.mid, jpeer);
    env->DeleteLocalRef(jpeer);
}

// ---------------------------------------------------------------------------
// JNI entry points
// ---------------------------------------------------------------------------

#define SS(h) reinterpret_cast<ScreenSessionJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL Java_com_driscord_jni_NativeScreenSession_create(
    JNIEnv*,
    jclass,
    jint bufMs,
    jint maxSyncMs
) {
    return reinterpret_cast<jlong>(new ScreenSessionJni(
        static_cast<int>(bufMs),
        static_cast<int>(maxSyncMs)
    ));
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_destroy(JNIEnv*, jclass, jlong h) {
    DriscordState::get().video_transport.channel.clear_video_sink();
    delete SS(h);
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeScreenSession_startSharing(
    JNIEnv* env,
    jclass,
    jlong h,
    jstring jTargetJson,
    jint maxW,
    jint maxH,
    jint fps,
    jint bitrateKbps,
    jint /*gopSize*/,
    jboolean shareAudio
) {
    const char* raw      = env->GetStringUTFChars(jTargetJson, nullptr);
    CaptureTarget target = target_from_json(json::parse(raw));
    env->ReleaseStringUTFChars(jTargetJson, raw);

    bool ok = SS(h)->session.start_sharing(
        target,
        static_cast<int>(maxW),
        static_cast<int>(maxH),
        static_cast<int>(fps),
        static_cast<int>(bitrateKbps),
        shareAudio == JNI_TRUE
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeScreenSession_stopSharing(JNIEnv*, jclass, jlong h) {
    SS(h)->session.stop_sharing();
    DriscordState::get().video_transport.channel.send_stop_stream();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeScreenSession_sharing(JNIEnv*, jclass, jlong h) {
    return SS(h)->session.sharing() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeScreenSession_sharingAudio(JNIEnv*, jclass, jlong h) {
    return SS(h)->session.sharing_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeScreenSession_forceKeyframe(JNIEnv*, jclass, jlong h) {
    SS(h)->session.force_keyframe();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_update(JNIEnv*, jclass, jlong h) {
    SS(h)->session.update();
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeScreenSession_activePeer(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(SS(h)->session.active_peer().c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeScreenSession_active(JNIEnv*, jclass, jlong h) {
    return SS(h)->session.active() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_reset(JNIEnv*, jclass, jlong h) {
    SS(h)->session.reset();
}


JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeScreenSession_resetAudioReceiver(JNIEnv*, jclass, jlong h) {
    SS(h)->session.reset_audio();
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeScreenSession_setStreamVolume(JNIEnv*, jclass, jlong h, jfloat vol) {
    SS(h)->session.audio_receiver()->set_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_jni_NativeScreenSession_streamVolume(JNIEnv*, jclass, jlong h) {
    return SS(h)->session.audio_receiver()->volume();
}

JNIEXPORT jstring JNICALL
Java_com_driscord_jni_NativeScreenSession_stats(JNIEnv* env, jclass, jlong h) {
    auto* s = SS(h);
    auto vs = s->session.video_stats();
    auto as = s->session.audio_stats();
    json j  = {
        {"width", s->session.last_width()},
        {"height", s->session.last_height()},
        {"measuredKbps", s->session.measured_kbps()},
        {"video", {{"queue", vs.queue_size}, {"drops", vs.drop_count}, {"misses", vs.miss_count}}},
        {"audio", {{"queue", as.queue_size}, {"drops", as.drop_count}, {"misses", as.miss_count}}}
    };
    return env->NewStringUTF(j.dump().c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeScreenSession_setOnFrame(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* s = SS(h);
    std::scoped_lock lk(s->cb_mutex);
    set_callback(env, s->on_frame_cb, cb, "invoke", "(Ljava/lang/String;[BII)V");
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_setOnFrameRemoved(
    JNIEnv* env,
    jclass,
    jlong h,
    jobject cb
) {
    auto* s = SS(h);
    std::scoped_lock lk(s->cb_mutex);
    set_callback(env, s->on_frame_removed_cb, cb, "invoke", "(Ljava/lang/String;)V");
}

} // extern "C"
