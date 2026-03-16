// JNI bridge for driscord_core.
// Each C++ component is wrapped individually. Kotlin (DriscordApp) owns orchestration.

#include <jni.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "audio/audio_mixer.hpp"
#include "audio/audio_receiver.hpp"
#include "audio/audio_sender.hpp"
#include "config.hpp"
#include "stream_defs.hpp"
#include "video/capture/screen_capture.hpp"
#include "video/screen_session.hpp"
#include "audio_transport.hpp"
#include "transport.hpp"
#include "video_transport.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// JNI callback helper
// ---------------------------------------------------------------------------

struct JniCallback {
    JavaVM*    jvm = nullptr;
    jobject    obj = nullptr;
    jmethodID  mid = nullptr;

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

static void set_callback(JNIEnv* env, JniCallback& cb, jobject listener,
                         const char* method, const char* sig) {
    if (cb.obj) { env->DeleteGlobalRef(cb.obj); cb.obj = nullptr; }
    if (!listener) return;
    env->GetJavaVM(&cb.jvm);
    cb.obj = env->NewGlobalRef(listener);
    cb.mid = env->GetMethodID(env->GetObjectClass(listener), method, sig);
}

static void fire_string(JniCallback& cb, std::mutex& mtx, const std::string& s) {
    std::scoped_lock lk(mtx);
    if (!cb.obj) return;
    auto* env = cb.attach();
    if (!env) return;
    jstring js = env->NewStringUTF(s.c_str());
    env->CallVoidMethod(cb.obj, cb.mid, js);
    env->DeleteLocalRef(js);
}

// ---------------------------------------------------------------------------
// Helper: parse CaptureTarget from JSON
// ---------------------------------------------------------------------------

static CaptureTarget target_from_json(const json& j) {
    CaptureTarget t;
    t.type   = j.value("type", 0) == 0 ? CaptureTarget::Monitor : CaptureTarget::Window;
    t.id     = j.value("id",     "");
    t.name   = j.value("name",   "");
    t.width  = j.value("width",  0);
    t.height = j.value("height", 0);
    t.x      = j.value("x",      0);
    t.y      = j.value("y",      0);
    return t;
}

// ===========================================================================
// TransportJni — com.driscord.NativeTransport
// ===========================================================================

struct TransportJni {
    Transport   transport;
    std::mutex  cb_mutex;
    JniCallback on_peer_joined;
    JniCallback on_peer_left;

    TransportJni() {
        transport.on_peer_joined([this](const std::string& id) {
            fire_string(on_peer_joined, cb_mutex, id);
        });
        transport.on_peer_left([this](const std::string& id) {
            fire_string(on_peer_left, cb_mutex, id);
        });
    }
};

// ===========================================================================
// AudioReceiverJni — com.driscord.NativeAudioReceiver
// ===========================================================================

struct AudioReceiverJni {
    AudioReceiver receiver;
    explicit AudioReceiverJni(int jitter_ms) : receiver(jitter_ms) {}
};

// ===========================================================================
// AudioTransportJni — com.driscord.NativeAudioTransport
// Internally routes audio packets to registered C++ receivers.
// ===========================================================================

struct AudioTransportJni {
    AudioTransport channel;

    std::mutex  recv_mutex;
    std::unordered_map<std::string, AudioReceiver*> voice_recv;
    AudioReceiver* screen_audio_recv = nullptr;

    explicit AudioTransportJni(TransportJni& t) : channel(t.transport) {
        channel.on_audio_received([this](const std::string& peer_id,
                                         const uint8_t* data, size_t len) {
            std::scoped_lock lk(recv_mutex);
            auto it = voice_recv.find(peer_id);
            if (it != voice_recv.end()) it->second->push_packet(data, len);
        });
        channel.on_screen_audio_received([this](const std::string&,
                                                 const uint8_t* data, size_t len) {
            std::scoped_lock lk(recv_mutex);
            if (screen_audio_recv) screen_audio_recv->push_packet(data, len);
        });
    }

    void register_voice(const std::string& peer_id, AudioReceiver* recv) {
        std::scoped_lock lk(recv_mutex);
        voice_recv[peer_id] = recv;
    }
    void unregister_voice(const std::string& peer_id) {
        std::scoped_lock lk(recv_mutex);
        voice_recv.erase(peer_id);
    }
    void set_screen_audio_recv(AudioReceiver* recv) {
        std::scoped_lock lk(recv_mutex);
        screen_audio_recv = recv;
    }
};

// ===========================================================================
// AudioSenderJni — com.driscord.NativeAudioSender
// ===========================================================================

struct AudioSenderJni {
    AudioSender         sender;
    AudioTransportJni*  audio_transport; // non-owning

    explicit AudioSenderJni(AudioTransportJni* at) : audio_transport(at) {}

    bool start() {
        return sender.start([this](const uint8_t* d, size_t l) {
            audio_transport->channel.send_audio(d, l);
        });
    }
};

// ===========================================================================
// VideoTransportJni — com.driscord.NativeVideoTransport
// ===========================================================================

struct VideoTransportJni {
    VideoTransport channel;
    ScreenSession* screen_session = nullptr; // non-owning, set by Kotlin
    std::atomic<bool> watching{false};

    std::mutex  cb_mutex;
    JniCallback on_streaming_peer;         // fires once per new streaming peer id
    JniCallback on_streaming_peer_removed; // fires when a streaming peer is removed

    std::mutex           streaming_mutex;
    std::set<std::string> seen_streaming;

    explicit VideoTransportJni(TransportJni& t) : channel(t.transport) {
        channel.on_video_received([this](const std::string& peer_id,
                                         const uint8_t* data, size_t len) {
            {
                std::scoped_lock lk(streaming_mutex);
                if (seen_streaming.insert(peer_id).second) {
                    fire_string(on_streaming_peer, cb_mutex, peer_id);
                }
            }
            if (watching.load(std::memory_order_relaxed) && screen_session) {
                screen_session->push_video_packet(peer_id, data, len);
            }
        });
        channel.on_keyframe_requested([this]() {
            if (screen_session && screen_session->sharing())
                screen_session->force_keyframe();
        });
        channel.on_video_channel_opened([this]() {
            if (screen_session && screen_session->sharing())
                screen_session->force_keyframe();
        });
    }

    void remove_streaming_peer(const std::string& peer_id) {
        bool was_present;
        {
            std::scoped_lock lk(streaming_mutex);
            was_present = seen_streaming.erase(peer_id) > 0;
        }
        if (was_present) {
            fire_string(on_streaming_peer_removed, cb_mutex, peer_id);
        }
    }
};

// ===========================================================================
// ScreenSessionJni — com.driscord.NativeScreenSession
// ===========================================================================

struct ScreenSessionJni {
    ScreenSession      session;
    VideoTransportJni* video_transport; // non-owning
    AudioTransportJni* audio_transport; // non-owning

    std::mutex  cb_mutex;
    JniCallback on_frame_cb;
    JniCallback on_frame_removed_cb;

    std::string last_peer;
    int last_w = 0, last_h = 0;

    ScreenSessionJni(int buf_ms, int max_sync_ms,
                     VideoTransportJni* vt, AudioTransportJni* at)
        : session(buf_ms, max_sync_ms), video_transport(vt), audio_transport(at)
    {
        session.set_keyframe_callback([this]() {
            video_transport->channel.send_keyframe_request();
        });
        // Wire screen_session into VideoTransportJni for frame routing
        video_transport->screen_session = &session;
    }

    bool start_sharing(const CaptureTarget& target, int max_w, int max_h,
                       int fps, int bitrate_kbps, bool share_audio) {
        return session.start_sharing(
            target, max_w, max_h, fps, bitrate_kbps, share_audio,
            [this](const uint8_t* d, size_t l) { video_transport->channel.send_video(d, l); },
            [this](const uint8_t* d, size_t l) { audio_transport->channel.send_screen_audio(d, l); }
        );
    }

    void update() {
        if (auto* frame = session.update()) {
            std::string peer = session.active_peer();
            if (!peer.empty()) {
                if (!last_peer.empty() && last_peer != peer)
                    fire_remove_frame(last_peer);
                last_w = frame->width;
                last_h = frame->height;
                fire_frame(peer, frame->rgba.data(), frame->width, frame->height);
                last_peer = peer;
            }
        }
        if (!last_peer.empty() && !session.active()) {
            fire_remove_frame(last_peer);
            last_peer.clear();
        }
    }

    void fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h) {
        std::scoped_lock lk(cb_mutex);
        if (!on_frame_cb.obj) return;
        auto* env = on_frame_cb.attach();
        if (!env) return;
        jstring jpeer = env->NewStringUTF(peer_id.c_str());
        jbyteArray jdata = env->NewByteArray(w * h * 4);
        env->SetByteArrayRegion(jdata, 0, w * h * 4, reinterpret_cast<const jbyte*>(rgba));
        env->CallVoidMethod(on_frame_cb.obj, on_frame_cb.mid, jpeer, jdata, (jint)w, (jint)h);
        env->DeleteLocalRef(jdata);
        env->DeleteLocalRef(jpeer);
    }

    void fire_remove_frame(const std::string& peer_id) {
        std::scoped_lock lk(cb_mutex);
        if (!on_frame_removed_cb.obj) return;
        auto* env = on_frame_removed_cb.attach();
        if (!env) return;
        jstring jpeer = env->NewStringUTF(peer_id.c_str());
        env->CallVoidMethod(on_frame_removed_cb.obj, on_frame_removed_cb.mid, jpeer);
        env->DeleteLocalRef(jpeer);
    }
};

// ---------------------------------------------------------------------------
// Cast helpers
// ---------------------------------------------------------------------------

#define TRANSPORT(h)      reinterpret_cast<TransportJni*>(h)
#define AUDIO_TRANSPORT(h) reinterpret_cast<AudioTransportJni*>(h)
#define VIDEO_TRANSPORT(h) reinterpret_cast<VideoTransportJni*>(h)
#define AUDIO_SENDER(h)   reinterpret_cast<AudioSenderJni*>(h)
#define AUDIO_MIXER(h)    reinterpret_cast<AudioMixer*>(h)
#define AUDIO_RECEIVER(h) reinterpret_cast<AudioReceiverJni*>(h)
#define SCREEN_SESSION(h) reinterpret_cast<ScreenSessionJni*>(h)

// ===========================================================================
// JNI exports
// ===========================================================================

extern "C" {

// ---------------------------------------------------------------------------
// NativeTransport
// ---------------------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeTransport_create(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new TransportJni());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_destroy(JNIEnv*, jclass, jlong h) {
    delete TRANSPORT(h);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_addTurnServer(JNIEnv* env, jclass, jlong h,
        jstring jUrl, jstring jUser, jstring jPass) {
    auto url  = env->GetStringUTFChars(jUrl,  nullptr);
    auto user = env->GetStringUTFChars(jUser, nullptr);
    auto pass = env->GetStringUTFChars(jPass, nullptr);
    TRANSPORT(h)->transport.add_turn_server(url, user, pass);
    env->ReleaseStringUTFChars(jUrl,  url);
    env->ReleaseStringUTFChars(jUser, user);
    env->ReleaseStringUTFChars(jPass, pass);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_connect(JNIEnv* env, jclass, jlong h, jstring jUrl) {
    auto url = env->GetStringUTFChars(jUrl, nullptr);
    TRANSPORT(h)->transport.connect(url);
    env->ReleaseStringUTFChars(jUrl, url);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_disconnect(JNIEnv*, jclass, jlong h) {
    TRANSPORT(h)->transport.disconnect();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeTransport_connected(JNIEnv*, jclass, jlong h) {
    return TRANSPORT(h)->transport.connected() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_NativeTransport_localId(JNIEnv* env, jclass, jlong h) {
    return env->NewStringUTF(TRANSPORT(h)->transport.local_id().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_driscord_NativeTransport_peers(JNIEnv* env, jclass, jlong h) {
    auto ps = TRANSPORT(h)->transport.peers();
    json arr = json::array();
    for (auto& p : ps) arr.push_back({{"id", p.id}, {"connected", p.primary_open}});
    return env->NewStringUTF(arr.dump().c_str());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_setOnPeerJoined(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* t = TRANSPORT(h);
    std::scoped_lock lk(t->cb_mutex);
    set_callback(env, t->on_peer_joined, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeTransport_setOnPeerLeft(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* t = TRANSPORT(h);
    std::scoped_lock lk(t->cb_mutex);
    set_callback(env, t->on_peer_left, cb, "invoke", "(Ljava/lang/String;)V");
}

// ---------------------------------------------------------------------------
// NativeAudioTransport
// ---------------------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeAudioTransport_create(JNIEnv*, jclass, jlong transportHandle) {
    return reinterpret_cast<jlong>(new AudioTransportJni(*TRANSPORT(transportHandle)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_destroy(JNIEnv*, jclass, jlong h) {
    delete AUDIO_TRANSPORT(h);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_sendAudio(JNIEnv* env, jclass, jlong h,
        jbyteArray jdata, jint len) {
    auto* buf = env->GetByteArrayElements(jdata, nullptr);
    AUDIO_TRANSPORT(h)->channel.send_audio(reinterpret_cast<const uint8_t*>(buf), len);
    env->ReleaseByteArrayElements(jdata, buf, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_registerVoiceReceiver(JNIEnv* env, jclass, jlong h,
        jstring jPeer, jlong receiverHandle) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    AUDIO_TRANSPORT(h)->register_voice(peer, &AUDIO_RECEIVER(receiverHandle)->receiver);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_unregisterVoiceReceiver(JNIEnv* env, jclass, jlong h,
        jstring jPeer) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    AUDIO_TRANSPORT(h)->unregister_voice(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioTransport_setScreenAudioReceiver(JNIEnv*, jclass,
        jlong audioHandle, jlong screenHandle) {
    AudioReceiver* recv = nullptr;
    if (screenHandle != 0)
        recv = SCREEN_SESSION(screenHandle)->session.audio_receiver();
    AUDIO_TRANSPORT(audioHandle)->set_screen_audio_recv(recv);
}

// ---------------------------------------------------------------------------
// NativeVideoTransport
// ---------------------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeVideoTransport_create(JNIEnv*, jclass, jlong transportHandle) {
    return reinterpret_cast<jlong>(new VideoTransportJni(*TRANSPORT(transportHandle)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_destroy(JNIEnv*, jclass, jlong h) {
    delete VIDEO_TRANSPORT(h);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_setWatching(JNIEnv*, jclass, jlong h, jboolean watching) {
    VIDEO_TRANSPORT(h)->watching.store(watching == JNI_TRUE, std::memory_order_relaxed);
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeVideoTransport_watching(JNIEnv*, jclass, jlong h) {
    return VIDEO_TRANSPORT(h)->watching.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_removeStreamingPeer(JNIEnv* env, jclass, jlong h, jstring jPeer) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    VIDEO_TRANSPORT(h)->remove_streaming_peer(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_sendKeyframeRequest(JNIEnv*, jclass, jlong h) {
    VIDEO_TRANSPORT(h)->channel.send_keyframe_request();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_setOnNewStreamingPeer(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* vt = VIDEO_TRANSPORT(h);
    std::scoped_lock lk(vt->cb_mutex);
    set_callback(env, vt->on_streaming_peer, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_setOnStreamingPeerRemoved(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* vt = VIDEO_TRANSPORT(h);
    std::scoped_lock lk(vt->cb_mutex);
    set_callback(env, vt->on_streaming_peer_removed, cb, "invoke", "(Ljava/lang/String;)V");
}

// ---------------------------------------------------------------------------
// NativeAudioSender
// ---------------------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeAudioSender_create(JNIEnv*, jclass, jlong audioTransportHandle) {
    return reinterpret_cast<jlong>(new AudioSenderJni(AUDIO_TRANSPORT(audioTransportHandle)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioSender_destroy(JNIEnv*, jclass, jlong h) {
    delete reinterpret_cast<AudioSenderJni*>(h);
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeAudioSender_start(JNIEnv*, jclass, jlong h) {
    return reinterpret_cast<AudioSenderJni*>(h)->start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioSender_stop(JNIEnv*, jclass, jlong h) {
    reinterpret_cast<AudioSenderJni*>(h)->sender.stop();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeAudioSender_muted(JNIEnv*, jclass, jlong h) {
    return reinterpret_cast<AudioSenderJni*>(h)->sender.muted() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioSender_setMuted(JNIEnv*, jclass, jlong h, jboolean muted) {
    reinterpret_cast<AudioSenderJni*>(h)->sender.set_muted(muted == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeAudioSender_inputLevel(JNIEnv*, jclass, jlong h) {
    return reinterpret_cast<AudioSenderJni*>(h)->sender.input_level();
}

// ---------------------------------------------------------------------------
// NativeAudioMixer
// ---------------------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeAudioMixer_create(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new AudioMixer());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_destroy(JNIEnv*, jclass, jlong h) {
    delete AUDIO_MIXER(h);
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeAudioMixer_start(JNIEnv*, jclass, jlong h) {
    return AUDIO_MIXER(h)->start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_stop(JNIEnv*, jclass, jlong h) {
    AUDIO_MIXER(h)->stop();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeAudioMixer_deafened(JNIEnv*, jclass, jlong h) {
    return AUDIO_MIXER(h)->deafened() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_setDeafened(JNIEnv*, jclass, jlong h, jboolean deaf) {
    AUDIO_MIXER(h)->set_deafened(deaf == JNI_TRUE);
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeAudioMixer_outputVolume(JNIEnv*, jclass, jlong h) {
    return AUDIO_MIXER(h)->output_volume();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_setOutputVolume(JNIEnv*, jclass, jlong h, jfloat vol) {
    AUDIO_MIXER(h)->set_output_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeAudioMixer_outputLevel(JNIEnv*, jclass, jlong h) {
    return AUDIO_MIXER(h)->output_level();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_addSource(JNIEnv*, jclass, jlong h, jlong receiverHandle) {
    AUDIO_MIXER(h)->add_source(&AUDIO_RECEIVER(receiverHandle)->receiver);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioMixer_removeSource(JNIEnv*, jclass, jlong h, jlong receiverHandle) {
    AUDIO_MIXER(h)->remove_source(&AUDIO_RECEIVER(receiverHandle)->receiver);
}

// ---------------------------------------------------------------------------
// NativeAudioReceiver
// ---------------------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeAudioReceiver_create(JNIEnv*, jclass, jint jitterMs) {
    return reinterpret_cast<jlong>(new AudioReceiverJni(static_cast<int>(jitterMs)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioReceiver_destroy(JNIEnv*, jclass, jlong h) {
    delete AUDIO_RECEIVER(h);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioReceiver_reset(JNIEnv*, jclass, jlong h) {
    AUDIO_RECEIVER(h)->receiver.reset();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeAudioReceiver_setVolume(JNIEnv*, jclass, jlong h, jfloat vol) {
    AUDIO_RECEIVER(h)->receiver.set_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeAudioReceiver_volume(JNIEnv*, jclass, jlong h) {
    return AUDIO_RECEIVER(h)->receiver.volume();
}

// ---------------------------------------------------------------------------
// NativeScreenSession
// ---------------------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_driscord_NativeScreenSession_create(JNIEnv*, jclass,
        jint bufMs, jint maxSyncMs,
        jlong videoTransportHandle, jlong audioTransportHandle) {
    return reinterpret_cast<jlong>(new ScreenSessionJni(
        static_cast<int>(bufMs), static_cast<int>(maxSyncMs),
        VIDEO_TRANSPORT(videoTransportHandle), AUDIO_TRANSPORT(audioTransportHandle)));
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeScreenSession_destroy(JNIEnv*, jclass, jlong h) {
    auto* s = SCREEN_SESSION(h);
    s->video_transport->screen_session = nullptr;
    delete s;
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeScreenSession_startSharing(JNIEnv* env, jclass, jlong h,
        jstring jTargetJson, jint maxW, jint maxH, jint fps, jint bitrateKbps, jboolean shareAudio) {
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
Java_com_driscord_NativeScreenSession_setOnFrameRemoved(JNIEnv* env, jclass, jlong h, jobject cb) {
    auto* s = SCREEN_SESSION(h);
    std::scoped_lock lk(s->cb_mutex);
    set_callback(env, s->on_frame_removed_cb, cb, "invoke", "(Ljava/lang/String;)V");
}

// ---------------------------------------------------------------------------
// NativeScreenCapture (static helpers, no handle)
// ---------------------------------------------------------------------------

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
            {"id",     t.id},   {"name",   t.name},
            {"width",  t.width}, {"height", t.height},
            {"x",      t.x},    {"y",      t.y}
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

    for (size_t i = 0; i < frame.data.size(); i += 4)
        std::swap(frame.data[i], frame.data[i + 2]);

    jbyteArray out = env->NewByteArray(static_cast<jsize>(frame.data.size()));
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(frame.data.size()),
                            reinterpret_cast<const jbyte*>(frame.data.data()));
    return out;
}

} // extern "C"
