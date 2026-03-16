// JNI bridge for driscord_core.
// Manages the full voice/video session without depending on App, ImGui, or OpenGL.
// Kotlin is responsible for all orchestration logic (the new "App").

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
#include "audio/capture/system_audio_capture.hpp"
#include "config.hpp"
#include "stream_defs.hpp"
#include "video/capture/screen_capture.hpp"
#include "video/screen_session.hpp"
#include "video/video_sender.hpp"
#include "voice_transport.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// JNI callback helpers
// ---------------------------------------------------------------------------

struct JniCallback {
    JavaVM* jvm = nullptr;
    jobject obj = nullptr;   // global ref
    jmethodID mid = nullptr;

    void clear(JNIEnv* env) {
        if (obj) { env->DeleteGlobalRef(obj); obj = nullptr; }
        jvm = nullptr; mid = nullptr;
    }

    JNIEnv* attach() const {
        if (!jvm) return nullptr;
        JNIEnv* env = nullptr;
        if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
            jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), nullptr);
        }
        return env;
    }
};

// ---------------------------------------------------------------------------
// Session: owns all C++ components, wired together.
// Kotlin DriscordApp calls JNI methods to control this.
// ---------------------------------------------------------------------------

struct Session {
    explicit Session(const Config& cfg)
        : config(cfg),
          screen_session(cfg.screen_buffer_ms, cfg.max_sync_gap_ms) {
        for (auto& ts : cfg.turn_servers) {
            transport.add_turn_server(ts.url, ts.user, ts.pass);
        }
        setup_transport_callbacks();
    }

    ~Session() = default;

    // --- config ---
    Config config;

    // --- core components ---
    VoiceTransport transport;
    AudioSender audio_sender;
    AudioMixer audio_mixer;
    ScreenSession screen_session;
    VideoSender video_sender;

    // --- voice receivers keyed by peer id ---
    mutable std::mutex voice_mutex;
    std::unordered_map<std::string, std::unique_ptr<AudioReceiver>> voice_receivers;

    mutable std::mutex peer_vol_mutex;
    std::unordered_map<std::string, float> peer_volumes;

    mutable std::mutex streaming_mutex;
    std::set<std::string> streaming_peers;

    std::atomic<bool> watching_stream{false};

    // --- JNI callbacks to Kotlin ---
    std::mutex cb_mutex;
    JniCallback on_frame_cb;       // void(peerId:String, rgba:ByteArray, w:Int, h:Int)
    JniCallback on_peer_remove_frame_cb; // void(peerId:String)  — frame removal
    JniCallback on_peer_joined_cb; // void(peerId:String)
    JniCallback on_peer_left_cb;   // void(peerId:String)
    JniCallback on_state_changed_cb; // void(state:Int)

    // last rendered peer (for update loop)
    std::string last_peer;
    int last_w = 0, last_h = 0;

    // ---------------------------------------------------------------------------
    // Setup
    // ---------------------------------------------------------------------------

    void setup_transport_callbacks() {
        transport.on_audio_received([this](const std::string& peer_id, const uint8_t* data, size_t len) {
            AudioReceiver* recv = get_or_create_voice(peer_id);
            recv->push_packet(data, len);
        });

        transport.on_video_received([this](const std::string& peer_id, const uint8_t* data, size_t len) {
            {
                std::scoped_lock lk(streaming_mutex);
                streaming_peers.insert(peer_id);
            }
            if (watching_stream.load(std::memory_order_relaxed)) {
                screen_session.push_video_packet(peer_id, data, len);
            }
        });

        transport.on_screen_audio_received([this](const std::string&, const uint8_t* data, size_t len) {
            if (watching_stream.load(std::memory_order_relaxed)) {
                screen_session.push_audio_packet(data, len);
            }
        });

        transport.on_peer_joined([this](const std::string& peer_id) {
            fire_peer_joined(peer_id);
        });

        transport.on_peer_left([this](const std::string& peer_id) {
            {
                std::scoped_lock lk(streaming_mutex);
                streaming_peers.erase(peer_id);
            }
            {
                std::scoped_lock lk(voice_mutex);
                auto it = voice_receivers.find(peer_id);
                if (it != voice_receivers.end()) {
                    audio_mixer.remove_source(it->second.get());
                    voice_receivers.erase(it);
                }
            }
            {
                std::scoped_lock lk(peer_vol_mutex);
                peer_volumes.erase(peer_id);
            }
            if (screen_session.active_peer() == peer_id) {
                screen_session.reset();
                fire_remove_frame(peer_id);
                last_peer.clear();
            }
            fire_peer_left(peer_id);
        });

        transport.on_video_channel_opened([this]() {
            if (video_sender.sharing()) video_sender.force_keyframe();
        });

        transport.on_keyframe_requested([this]() {
            if (video_sender.sharing()) video_sender.force_keyframe();
        });

        screen_session.set_keyframe_callback([this]() { transport.send_keyframe_request(); });
    }

    // Called from Kotlin update loop
    void update() {
        // Transition Connecting → Connected
        if (!transport.connected()) return;

        if (auto* frame = screen_session.update()) {
            std::string peer = screen_session.active_peer();
            if (!peer.empty()) {
                if (!last_peer.empty() && last_peer != peer) {
                    fire_remove_frame(last_peer);
                }
                last_w = frame->width;
                last_h = frame->height;
                fire_frame(peer, frame->rgba.data(), frame->width, frame->height);
                last_peer = peer;
            }
        }
        if (!last_peer.empty() && !screen_session.active()) {
            fire_remove_frame(last_peer);
            last_peer.clear();
        }
    }

    // Connect (called once transport is set up)
    void connect(const std::string& url) {
        transport.connect(url);
    }

    void disconnect() {
        video_sender.stop();
        audio_sender.stop();
        audio_mixer.stop();
        watching_stream.store(false);
        transport.disconnect();
        {
            std::scoped_lock lk(streaming_mutex);
            streaming_peers.clear();
        }
        {
            std::scoped_lock lk(voice_mutex);
            voice_receivers.clear();
        }
        screen_session.reset();
        if (!last_peer.empty()) {
            fire_remove_frame(last_peer);
            last_peer.clear();
        }
    }

    bool start_audio() {
        bool s = audio_sender.start([this](const uint8_t* data, size_t len) {
            transport.send_audio(data, len);
        });
        bool m = audio_mixer.start();
        return s && m;
    }

    void join_stream() {
        if (!watching_stream.load()) {
            screen_session.audio_receiver()->reset();
            watching_stream.store(true);
            audio_mixer.add_source(screen_session.audio_receiver());
        }
    }

    void leave_stream() {
        if (watching_stream.load()) {
            watching_stream.store(false);
            audio_mixer.remove_source(screen_session.audio_receiver());
            std::string peer = screen_session.active_peer();
            if (!peer.empty()) {
                fire_remove_frame(peer);
                if (last_peer == peer) last_peer.clear();
            }
        }
    }

    bool start_sharing(const CaptureTarget& target, StreamQuality quality, int fps, bool share_audio) {
        int max_w, max_h;
        if (quality == StreamQuality::Source) {
            max_w = 7680; max_h = 4320;
        } else {
            max_w = kStreamPresets[static_cast<int>(quality)].width;
            max_h = kStreamPresets[static_cast<int>(quality)].height;
        }
        return video_sender.start(
            target, max_w, max_h, fps, config.video_bitrate_kbps, share_audio,
            [this](const uint8_t* d, size_t l) { transport.send_video(d, l); },
            [this](const uint8_t* d, size_t l) { transport.send_screen_audio(d, l); }
        );
    }

    AudioReceiver* get_or_create_voice(const std::string& peer_id) {
        std::scoped_lock lk(voice_mutex);
        auto& slot = voice_receivers[peer_id];
        if (!slot) {
            slot = std::make_unique<AudioReceiver>(config.voice_jitter_ms);
            float vol = [&] {
                std::scoped_lock lk2(peer_vol_mutex);
                auto it = peer_volumes.find(peer_id);
                return it != peer_volumes.end() ? it->second : 1.0f;
            }();
            if (vol != 1.0f) slot->set_volume(vol);
            audio_mixer.add_source(slot.get());
        }
        return slot.get();
    }

    float peer_volume(const std::string& peer_id) const {
        std::scoped_lock lk(peer_vol_mutex);
        auto it = peer_volumes.find(peer_id);
        return it != peer_volumes.end() ? it->second : 1.0f;
    }

    void set_peer_volume(const std::string& peer_id, float vol) {
        { std::scoped_lock lk(peer_vol_mutex); peer_volumes[peer_id] = vol; }
        std::scoped_lock lk(voice_mutex);
        auto it = voice_receivers.find(peer_id);
        if (it != voice_receivers.end()) it->second->set_volume(vol);
    }

    std::vector<VoiceTransport::PeerInfo> peers() const { return transport.peers(); }

    std::vector<std::string> get_streaming_peers() const {
        std::scoped_lock lk(streaming_mutex);
        return {streaming_peers.begin(), streaming_peers.end()};
    }

    // ---------------------------------------------------------------------------
    // JNI fire helpers (call Kotlin lambdas)
    // ---------------------------------------------------------------------------

    void fire_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h) {
        std::scoped_lock lk(cb_mutex);
        if (!on_frame_cb.obj) return;
        JNIEnv* env = on_frame_cb.attach();
        if (!env) return;
        jstring jpeer = env->NewStringUTF(peer_id.c_str());
        jint size = w * h * 4;
        jbyteArray jdata = env->NewByteArray(size);
        env->SetByteArrayRegion(jdata, 0, size, reinterpret_cast<const jbyte*>(rgba));
        env->CallVoidMethod(on_frame_cb.obj, on_frame_cb.mid, jpeer, jdata, (jint)w, (jint)h);
        env->DeleteLocalRef(jdata);
        env->DeleteLocalRef(jpeer);
    }

    void fire_remove_frame(const std::string& peer_id) {
        std::scoped_lock lk(cb_mutex);
        if (!on_peer_remove_frame_cb.obj) return;
        JNIEnv* env = on_peer_remove_frame_cb.attach();
        if (!env) return;
        jstring jpeer = env->NewStringUTF(peer_id.c_str());
        env->CallVoidMethod(on_peer_remove_frame_cb.obj, on_peer_remove_frame_cb.mid, jpeer);
        env->DeleteLocalRef(jpeer);
    }

    void fire_peer_joined(const std::string& peer_id) {
        std::scoped_lock lk(cb_mutex);
        if (!on_peer_joined_cb.obj) return;
        JNIEnv* env = on_peer_joined_cb.attach();
        if (!env) return;
        jstring jpeer = env->NewStringUTF(peer_id.c_str());
        env->CallVoidMethod(on_peer_joined_cb.obj, on_peer_joined_cb.mid, jpeer);
        env->DeleteLocalRef(jpeer);
    }

    void fire_peer_left(const std::string& peer_id) {
        std::scoped_lock lk(cb_mutex);
        if (!on_peer_left_cb.obj) return;
        JNIEnv* env = on_peer_left_cb.attach();
        if (!env) return;
        jstring jpeer = env->NewStringUTF(peer_id.c_str());
        env->CallVoidMethod(on_peer_left_cb.obj, on_peer_left_cb.mid, jpeer);
        env->DeleteLocalRef(jpeer);
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Session* to_session(jlong handle) {
    return reinterpret_cast<Session*>(handle);
}

static CaptureTarget target_from_json(const json& j) {
    CaptureTarget t;
    int type_int = j.value("type", 0);
    t.type = (type_int == 0) ? CaptureTarget::Monitor : CaptureTarget::Window;
    t.id    = j.value("id", "");
    t.name  = j.value("name", "");
    t.width = j.value("width", 0);
    t.height= j.value("height", 0);
    t.x     = j.value("x", 0);
    t.y     = j.value("y", 0);
    return t;
}

static void set_jni_callback(JNIEnv* env, JniCallback& cb, jobject listener,
                              const char* method, const char* sig) {
    if (cb.obj) { env->DeleteGlobalRef(cb.obj); cb.obj = nullptr; }
    if (!listener) return;
    env->GetJavaVM(&cb.jvm);
    cb.obj = env->NewGlobalRef(listener);
    jclass cls = env->GetObjectClass(listener);
    cb.mid = env->GetMethodID(cls, method, sig);
}

// ---------------------------------------------------------------------------
// JNI exports — package com.driscord, class NativeSession
// ---------------------------------------------------------------------------

extern "C" {

// Session lifecycle
JNIEXPORT jlong JNICALL
Java_com_driscord_NativeSession_create(JNIEnv* env, jclass,
        jstring jHost, jint port,
        jint screenFps, jint bitrateKbps,
        jint voiceJitterMs, jint screenBufMs, jint maxSyncGapMs) {
    const char* host = env->GetStringUTFChars(jHost, nullptr);
    Config cfg;
    cfg.server_host       = host;
    cfg.server_port       = static_cast<int>(port);
    cfg.screen_fps        = static_cast<int>(screenFps);
    cfg.video_bitrate_kbps= static_cast<int>(bitrateKbps);
    cfg.voice_jitter_ms   = static_cast<int>(voiceJitterMs);
    cfg.screen_buffer_ms  = static_cast<int>(screenBufMs);
    cfg.max_sync_gap_ms   = static_cast<int>(maxSyncGapMs);
    env->ReleaseStringUTFChars(jHost, host);
    return reinterpret_cast<jlong>(new Session(cfg));
}

// Accepts a JSON array: [{"url":"...","user":"...","pass":"..."},...]
JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_setTurnServers(JNIEnv* env, jclass, jlong handle, jstring jJson) {
    const char* raw = env->GetStringUTFChars(jJson, nullptr);
    try {
        auto arr = json::parse(raw);
        auto* s = to_session(handle);
        for (auto& entry : arr) {
            std::string url  = entry.value("url",  "");
            std::string user = entry.value("user", "");
            std::string pass = entry.value("pass", "");
            if (!url.empty()) {
                s->transport.add_turn_server(url, user, pass);
            }
        }
    } catch (...) {}
    env->ReleaseStringUTFChars(jJson, raw);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_destroy(JNIEnv*, jclass, jlong handle) {
    delete to_session(handle);
}

// Connection
JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_connect(JNIEnv* env, jclass, jlong handle, jstring jUrl) {
    const char* url = env->GetStringUTFChars(jUrl, nullptr);
    to_session(handle)->connect(url);
    env->ReleaseStringUTFChars(jUrl, url);
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_disconnect(JNIEnv*, jclass, jlong handle) {
    to_session(handle)->disconnect();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeSession_connected(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->transport.connected() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_driscord_NativeSession_localId(JNIEnv* env, jclass, jlong handle) {
    return env->NewStringUTF(to_session(handle)->transport.local_id().c_str());
}

// Update loop (call ~every 16ms from Kotlin)
JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_update(JNIEnv*, jclass, jlong handle) {
    to_session(handle)->update();
}

// Audio
JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeSession_startAudio(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->start_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_toggleMute(JNIEnv*, jclass, jlong handle) {
    auto* s = to_session(handle);
    s->audio_sender.set_muted(!s->audio_sender.muted());
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_toggleDeafen(JNIEnv*, jclass, jlong handle) {
    auto* s = to_session(handle);
    bool deaf = !s->audio_mixer.deafened();
    s->audio_mixer.set_deafened(deaf);
    if (deaf) s->audio_sender.set_muted(true);
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeSession_muted(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->audio_sender.muted() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeSession_deafened(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->audio_mixer.deafened() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_setVolume(JNIEnv*, jclass, jlong handle, jfloat vol) {
    to_session(handle)->audio_mixer.set_output_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeSession_volume(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->audio_mixer.output_volume();
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeSession_inputLevel(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->audio_sender.input_level();
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeSession_outputLevel(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->audio_mixer.output_level();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_setPeerVolume(JNIEnv* env, jclass, jlong handle,
        jstring jPeer, jfloat vol) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    to_session(handle)->set_peer_volume(peer, static_cast<float>(vol));
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeSession_peerVolume(JNIEnv* env, jclass, jlong handle, jstring jPeer) {
    const char* peer = env->GetStringUTFChars(jPeer, nullptr);
    float vol = to_session(handle)->peer_volume(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
    return vol;
}

// Stream (watching)
JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_joinStream(JNIEnv*, jclass, jlong handle) {
    to_session(handle)->join_stream();
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_leaveStream(JNIEnv*, jclass, jlong handle) {
    to_session(handle)->leave_stream();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeSession_watchingStream(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->watching_stream.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_setStreamVolume(JNIEnv*, jclass, jlong handle, jfloat vol) {
    to_session(handle)->screen_session.audio_receiver()->set_volume(static_cast<float>(vol));
}

JNIEXPORT jfloat JNICALL
Java_com_driscord_NativeSession_streamVolume(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->screen_session.audio_receiver()->volume();
}

// Sharing (sending screen)
JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeSession_startSharing(JNIEnv* env, jclass, jlong handle,
        jstring jTargetJson, jint quality, jint fps, jboolean shareAudio) {
    const char* raw = env->GetStringUTFChars(jTargetJson, nullptr);
    CaptureTarget target = target_from_json(json::parse(raw));
    env->ReleaseStringUTFChars(jTargetJson, raw);
    bool ok = to_session(handle)->start_sharing(
        target, static_cast<StreamQuality>(quality), static_cast<int>(fps),
        shareAudio != JNI_FALSE);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_stopSharing(JNIEnv*, jclass, jlong handle) {
    to_session(handle)->video_sender.stop();
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeSession_sharing(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->video_sender.sharing() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeSession_sharingAudio(JNIEnv*, jclass, jlong handle) {
    return to_session(handle)->video_sender.sharing_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_NativeSession_systemAudioAvailable(JNIEnv*, jclass) {
    return SystemAudioCapture::available() ? JNI_TRUE : JNI_FALSE;
}

// Peers
JNIEXPORT jstring JNICALL
Java_com_driscord_NativeSession_peers(JNIEnv* env, jclass, jlong handle) {
    auto ps = to_session(handle)->peers();
    json arr = json::array();
    for (auto& p : ps) arr.push_back({{"id", p.id}, {"connected", p.dc_open}});
    return env->NewStringUTF(arr.dump().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_driscord_NativeSession_streamingPeers(JNIEnv* env, jclass, jlong handle) {
    auto sp = to_session(handle)->get_streaming_peers();
    json arr = json::array();
    for (auto& s : sp) arr.push_back(s);
    return env->NewStringUTF(arr.dump().c_str());
}

// Stats
JNIEXPORT jstring JNICALL
Java_com_driscord_NativeSession_streamStats(JNIEnv* env, jclass, jlong handle) {
    auto* s = to_session(handle);
    auto vs = s->screen_session.video_stats();
    auto as = s->screen_session.audio_stats();
    json j = {
        {"width", s->last_w}, {"height", s->last_h},
        {"measuredKbps", s->screen_session.measured_kbps()},
        {"video", {
            {"queue", vs.queue_size}, {"bufMs", vs.buffered_ms},
            {"drops", vs.drop_count}, {"misses", vs.miss_count}
        }},
        {"audio", {
            {"queue", as.queue_size}, {"bufMs", as.buffered_ms},
            {"drops", as.drop_count}, {"misses", as.miss_count}
        }}
    };
    return env->NewStringUTF(j.dump().c_str());
}

// Screen capture helpers (no session needed)
JNIEXPORT jstring JNICALL
Java_com_driscord_NativeSession_listCaptureTargets(JNIEnv* env, jclass) {
    auto targets = ScreenCapture::list_targets();
    json arr = json::array();
    for (auto& t : targets) {
        arr.push_back({
            {"type", t.type == CaptureTarget::Monitor ? 0 : 1},
            {"id", t.id}, {"name", t.name},
            {"width", t.width}, {"height", t.height},
            {"x", t.x}, {"y", t.y}
        });
    }
    return env->NewStringUTF(arr.dump().c_str());
}

JNIEXPORT jbyteArray JNICALL
Java_com_driscord_NativeSession_grabThumbnail(JNIEnv* env, jclass,
        jstring jTargetJson, jint maxW, jint maxH) {
    const char* raw = env->GetStringUTFChars(jTargetJson, nullptr);
    CaptureTarget target = target_from_json(json::parse(raw));
    env->ReleaseStringUTFChars(jTargetJson, raw);

    auto frame = ScreenCapture::grab_thumbnail(target, static_cast<int>(maxW), static_cast<int>(maxH));
    if (frame.data.empty()) return nullptr;

    // BGRA → RGBA
    for (size_t i = 0; i < frame.data.size(); i += 4)
        std::swap(frame.data[i], frame.data[i + 2]);

    jbyteArray out = env->NewByteArray(static_cast<jsize>(frame.data.size()));
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(frame.data.size()),
                            reinterpret_cast<const jbyte*>(frame.data.data()));
    return out;
}

// Callbacks — Kotlin passes a lambda object
// Signature: (peerId: String, rgba: ByteArray, w: Int, h: Int) -> Unit
JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_setOnFrame(JNIEnv* env, jclass, jlong handle, jobject cb) {
    auto* s = to_session(handle);
    std::scoped_lock lk(s->cb_mutex);
    set_jni_callback(env, s->on_frame_cb, cb, "invoke",
                     "(Ljava/lang/String;[BII)V");
}

// Signature: (peerId: String) -> Unit
JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_setOnFrameRemoved(JNIEnv* env, jclass, jlong handle, jobject cb) {
    auto* s = to_session(handle);
    std::scoped_lock lk(s->cb_mutex);
    set_jni_callback(env, s->on_peer_remove_frame_cb, cb, "invoke",
                     "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_setOnPeerJoined(JNIEnv* env, jclass, jlong handle, jobject cb) {
    auto* s = to_session(handle);
    std::scoped_lock lk(s->cb_mutex);
    set_jni_callback(env, s->on_peer_joined_cb, cb, "invoke", "(Ljava/lang/String;)V");
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeSession_setOnPeerLeft(JNIEnv* env, jclass, jlong handle, jobject cb) {
    auto* s = to_session(handle);
    std::scoped_lock lk(s->cb_mutex);
    set_jni_callback(env, s->on_peer_left_cb, cb, "invoke", "(Ljava/lang/String;)V");
}

} // extern "C"
