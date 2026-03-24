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

void ScreenSessionJni::join_stream(const std::string& peer_id) {
    watched_peers.insert(peer_id);
    auto& at = DriscordState::get().audio_transport.channel;
    auto& vt = DriscordState::get().video_transport.channel;
    at.set_screen_audio_recv(peer_id, session.audio_receiver());  // single shared multi-peer receiver
    at.add_screen_audio_to_mixer(peer_id);
    vt.add_watched_peer(peer_id);
    vt.send_keyframe_request();
}

void ScreenSessionJni::leave_stream() {
    auto& at = DriscordState::get().audio_transport.channel;
    auto& vt = DriscordState::get().video_transport.channel;
    vt.clear_watched_peers();
    for (const auto& pid : watched_peers) {
        at.remove_screen_audio_from_mixer(pid);
        at.unset_screen_audio_recv(pid);
    }
    session.reset();
    watched_peers.clear();
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

#define SS() (*DriscordState::get().screen_session)

extern "C" {

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_init(
    JNIEnv*,
    jclass,
    jint bufMs,
    jint maxSyncMs
) {
    DriscordState::get().screen_session.emplace(
        static_cast<int>(bufMs),
        static_cast<int>(maxSyncMs)
    );
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_deinit(JNIEnv*, jclass) {
    DriscordState::get().video_transport.channel.clear_video_sink();
    DriscordState::get().screen_session.reset();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeScreenSession_startSharing(
    JNIEnv* env,
    jclass,
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

    bool ok = SS().session.start_sharing(
        target,
        static_cast<int>(maxW),
        static_cast<int>(maxH),
        static_cast<int>(fps),
        static_cast<int>(bitrateKbps),
        shareAudio == JNI_TRUE
    );
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_stopSharing(JNIEnv*, jclass) {
    SS().session.stop_sharing();
    DriscordState::get().video_transport.channel.send_stop_stream();
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeScreenSession_sharing(JNIEnv*, jclass) {
    return SS().session.sharing() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeScreenSession_sharingAudio(JNIEnv*, jclass) {
    return SS().session.sharing_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_forceKeyframe(JNIEnv*, jclass) {
    SS().session.force_keyframe();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_update(JNIEnv*, jclass) {
    SS().session.update();
}

JNIEXPORT jstring JNICALL Java_com_driscord_jni_NativeScreenSession_activePeer(
    JNIEnv* env, jclass
) {
    return env->NewStringUTF(SS().session.active_peer().c_str());
}

JNIEXPORT jboolean JNICALL Java_com_driscord_jni_NativeScreenSession_active(JNIEnv*, jclass) {
    return SS().session.active() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_reset(JNIEnv*, jclass) {
    SS().session.reset();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_resetAudioReceiver(
    JNIEnv*, jclass
) {
    SS().session.reset_audio();
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_setStreamVolume(
    JNIEnv* env, jclass, jstring jPeerId, jfloat vol
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    SS().session.audio_receiver()->set_volume(peer, static_cast<float>(vol));
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT jfloat JNICALL Java_com_driscord_jni_NativeScreenSession_streamVolume(JNIEnv*, jclass) {
    return SS().session.audio_receiver()->volume();
}

JNIEXPORT jstring JNICALL Java_com_driscord_jni_NativeScreenSession_stats(
    JNIEnv* env, jclass
) {
    auto& s = SS();
    auto vs = s.session.video_stats();
    auto as = s.session.audio_stats();
    json j  = {
        {"width", s.session.last_width()},
        {"height", s.session.last_height()},
        {"measuredKbps", s.session.measured_kbps()},
        {"video", {{"queue", vs.queue_size}, {"drops", vs.drop_count}, {"misses", vs.miss_count}}},
        {"audio", {{"queue", as.queue_size}, {"drops", as.drop_count}, {"misses", as.miss_count}}}
    };
    return env->NewStringUTF(j.dump().c_str());
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_setOnFrame(
    JNIEnv* env, jclass, jobject cb
) {
    auto& s = SS();
    std::scoped_lock lk(s.cb_mutex);
    set_callback(env, s.on_frame_cb, cb, "invoke", "(Ljava/lang/String;[BII)V");
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_setOnFrameRemoved(
    JNIEnv* env, jclass, jobject cb
) {
    auto& s = SS();
    std::scoped_lock lk(s.cb_mutex);
    set_callback(env, s.on_frame_removed_cb, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_joinStream(
    JNIEnv* env, jclass, jstring jPeerId
) {
    const char* peer = env->GetStringUTFChars(jPeerId, nullptr);
    SS().join_stream(peer);
    env->ReleaseStringUTFChars(jPeerId, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_jni_NativeScreenSession_leaveStream(JNIEnv*, jclass) {
    SS().leave_stream();
}

} // extern "C"
