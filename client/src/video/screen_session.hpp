#pragma once

#include "audio/audio.hpp"
#include "video/screen.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

// ScreenSession combines ScreenSender + ScreenReceiver into a self-contained
// session object with no JNI/platform dependencies.
//
// Transport adapters are injected as callbacks at construction time:
//   send_video       — transmit an encoded video chunk to peers
//   send_screen_audio — transmit an encoded screen-audio chunk
//   on_keyframe_req  — ask the remote sender to emit a keyframe
//
// UI integration: set_on_frame / set_on_frame_removed are called from
// update() (which must be driven by the render/UI thread).
class ScreenSession {
public:
    using SendCb      = std::function<void(const uint8_t*, size_t)>;
    using OnFrameCb   = std::function<void(const std::string& peer_id,
                                           const uint8_t* rgba, int w, int h)>;
    using OnRemovedCb = std::function<void(const std::string& peer_id)>;

    ScreenSession(
        int buf_ms,
        int max_sync_ms,
        SendCb           send_video,
        std::function<void()> on_keyframe_req,
        SendCb           send_screen_audio
    );
    ~ScreenSession() = default;

    ScreenSession(const ScreenSession&) = delete;
    ScreenSession& operator=(const ScreenSession&) = delete;

    // ---- sender ----
    bool start_sharing(
        const CaptureTarget& target,
        int max_w, int max_h,
        int fps, int bitrate_kbps,
        int gop_size,
        bool share_audio
    );
    void stop_sharing();
    bool sharing()       const { return sender_.sharing(); }
    bool sharing_audio() const { return sender_.sharing_audio(); }
    void force_keyframe()      { sender_.force_keyframe(); }
    int  sender_kbps()   const { return sender_.sender_kbps(); }

    // ---- receiver (called from network/transport thread) ----
    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len);
    void push_audio_packet(const uint8_t* data, size_t len);

    // ---- UI / render thread ----
    // Advances the jitter buffer by one frame and fires on_frame / on_removed
    // callbacks if the active peer or content has changed.
    void update();

    void set_on_frame(OnFrameCb cb);
    void set_on_frame_removed(OnRemovedCb cb);

    // ---- queries ----
    std::string active_peer()   const { return receiver_.active_peer(); }
    bool        active()        const { return receiver_.active(); }
    int         measured_kbps() const { return receiver_.measured_kbps(); }
    int         last_width()    const { return last_w_; }
    int         last_height()   const { return last_h_; }

    std::shared_ptr<AudioReceiver>       audio_receiver()       { return receiver_.audio_receiver(); }
    std::shared_ptr<const AudioReceiver> audio_receiver() const { return receiver_.audio_receiver(); }

    VideoJitter::Stats   video_stats() const { return receiver_.video_stats(); }
    AudioReceiver::Stats audio_stats() const { return receiver_.audio_stats(); }

    void reset();
    void reset_audio();

private:
    ScreenSender   sender_;
    ScreenReceiver receiver_;

    SendCb send_video_;
    SendCb send_screen_audio_;

    std::string last_peer_;
    int         last_w_ = 0;
    int         last_h_ = 0;

    mutable std::mutex cb_mutex_;
    OnFrameCb          on_frame_cb_;
    OnRemovedCb        on_frame_removed_cb_;
};
