#pragma once

#include "audio/audio_receiver.hpp"
#include "video/video_receiver.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Receiver half of a screen-share session: reassembles video + audio and
// presents them in sync to the caller.
//
// A/V sync strategy (video-side):
//   - If the next video frame is more than kHoldThresholdMs ahead of the last
//     audio frame delivered to the mixer, freeze video until audio catches up.
//   - If audio is lagging more than kDrainThresholdMs behind video, stale
//     audio frames are discarded so catch-up happens quickly.
class ScreenReceiver {
public:
    // A/V sync thresholds (milliseconds).
    static constexpr int64_t kHoldThresholdMs  = 150;
    static constexpr int64_t kDrainThresholdMs = 300;

    ScreenReceiver(int buffer_ms, int max_sync_gap_ms);
    ~ScreenReceiver() = default;

    ScreenReceiver(const ScreenReceiver&) = delete;
    ScreenReceiver& operator=(const ScreenReceiver&) = delete;

    void push_video_packet(const std::string& peer_id, const uint8_t* data, size_t len);
    void push_audio_packet(const uint8_t* data, size_t len);

    // Advance playback by one video frame. Returns nullptr when nothing is ready.
    // Implements A/V sync: may hold the current frame if video is ahead of audio.
    const VideoJitter::Frame* update();

    std::shared_ptr<AudioReceiver> audio_receiver() { return audio_recv_; }
    std::shared_ptr<const AudioReceiver> audio_receiver() const { return audio_recv_; }

    void set_keyframe_callback(std::function<void()> fn);

    std::string active_peer() const;
    bool active() const;
    int measured_kbps() const;

    VideoJitter::Stats video_stats() const;
    AudioReceiver::Stats audio_stats() const;

    void reset();

private:
    VideoReceiver video_recv_;
    std::shared_ptr<AudioReceiver> audio_recv_;

    // A/V sync: true while we're freezing video waiting for audio to catch up.
    bool waiting_for_audio_ = false;
};
