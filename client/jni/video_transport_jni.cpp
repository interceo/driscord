#include "video_transport_jni.hpp"

VideoTransportJni::VideoTransportJni(TransportJni& t) : channel(t.transport) {
    channel.on_video_received([this](const std::string& peer_id, const uint8_t* data, size_t len) {
        {
            std::scoped_lock lk(streaming_mutex);
            if (seen_streaming.insert(peer_id).second) {
                fire_string(on_streaming_peer, cb_mutex, peer_id);
            }
        }
        if (watching.load(std::memory_order_relaxed)) {
            std::scoped_lock lk(sink_mutex);
            if (on_video_packet) {
                on_video_packet(peer_id, data, len);
            }
        }
    });
    channel.on_keyframe_requested([this]() {
        std::scoped_lock lk(sink_mutex);
        if (on_keyframe_needed) {
            on_keyframe_needed();
        }
    });
    channel.on_video_channel_opened([this]() {
        std::scoped_lock lk(sink_mutex);
        if (on_keyframe_needed) {
            on_keyframe_needed();
        }
    });
}

void VideoTransportJni::remove_streaming_peer(const std::string& peer_id) {
    bool was_present;
    {
        std::scoped_lock lk(streaming_mutex);
        was_present = seen_streaming.erase(peer_id) > 0;
    }
    if (was_present) {
        fire_string(on_streaming_peer_removed, cb_mutex, peer_id);
    }
}

void VideoTransportJni::set_video_sink(VideoPacketCb video_cb, KeyframeCb kf_cb) {
    std::scoped_lock lk(sink_mutex);
    on_video_packet = std::move(video_cb);
    on_keyframe_needed = std::move(kf_cb);
}

void VideoTransportJni::clear_video_sink() {
    std::scoped_lock lk(sink_mutex);
    on_video_packet = nullptr;
    on_keyframe_needed = nullptr;
}

#define VIDEO_TRANSPORT(h) reinterpret_cast<VideoTransportJni*>(h)

extern "C" {

JNIEXPORT jlong JNICALL Java_com_driscord_NativeVideoTransport_create(JNIEnv*, jclass, jlong transportHandle) {
    return reinterpret_cast<jlong>(new VideoTransportJni(*reinterpret_cast<TransportJni*>(transportHandle)));
}

JNIEXPORT void JNICALL Java_com_driscord_NativeVideoTransport_destroy(JNIEnv*, jclass, jlong h) {
    delete VIDEO_TRANSPORT(h);
}

JNIEXPORT void JNICALL Java_com_driscord_NativeVideoTransport_setWatching(JNIEnv*, jclass, jlong h, jboolean watching) {
    VIDEO_TRANSPORT(h)->watching.store(watching == JNI_TRUE, std::memory_order_relaxed);
}

JNIEXPORT jboolean JNICALL Java_com_driscord_NativeVideoTransport_watching(JNIEnv*, jclass, jlong h) {
    return VIDEO_TRANSPORT(h)->watching.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_driscord_NativeVideoTransport_removeStreamingPeer(JNIEnv* env, jclass, jlong h, jstring jPeer) {
    auto peer = env->GetStringUTFChars(jPeer, nullptr);
    VIDEO_TRANSPORT(h)->remove_streaming_peer(peer);
    env->ReleaseStringUTFChars(jPeer, peer);
}

JNIEXPORT void JNICALL Java_com_driscord_NativeVideoTransport_sendKeyframeRequest(JNIEnv*, jclass, jlong h) {
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

}  // extern "C"
