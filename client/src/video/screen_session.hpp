#pragma once

#include "audio/audio_receiver.hpp"
#include "video/video_receiver.hpp"

#include <functional>
#include <string>

class ScreenSession {
public:
    ScreenSession(int buffer_ms, int max_sync_gap_ms)
        : video_(buffer_ms, max_sync_gap_ms), audio_(buffer_ms, /*channels=*/2) {}

    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len) {
        video_.push_video_packet(peer_id, data, len);
    }

    void push_audio_packet(const uint8_t* data, size_t len) { audio_.push_packet(data, len); }

    const VideoJitter::Frame* update() { return video_.update(); }

    AudioReceiver* audio_receiver() { return &audio_; }
    const AudioReceiver* audio_receiver() const { return &audio_; }

    void set_keyframe_callback(std::function<void()> fn) { video_.set_keyframe_callback(std::move(fn)); }

    std::string active_peer() const { return video_.active_peer(); }
    bool active() const { return video_.active(); }
    int measured_kbps() const { return video_.measured_kbps(); }

    VideoJitter::Stats video_stats() const { return video_.video_stats(); }
    AudioReceiver::Stats audio_stats() const { return audio_.stats(); }

    void reset() {
        video_.reset();
        audio_.reset();
    }

private:
    VideoReceiver video_;
    AudioReceiver audio_;
};
