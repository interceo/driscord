#pragma once

#include "audio/audio_jitter.hpp"
#include "utils/jitter.hpp"
#include "utils/time.hpp"

#include <atomic>
#include <optional>
#include <vector>

class ScreenJitter {
public:
    // Estimated frame duration for buffered_ms reporting (30fps ≈ 33ms).
    static constexpr int kVideoFrameMs = 33;

    struct VideoFrame {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
    };

    ScreenJitter(int buffer_ms, int /*max_sync_gap_ms*/, int sample_rate = opus::kSampleRate)
        : audio_(static_cast<size_t>(buffer_ms), sample_rate), video_(buffer_ms, kVideoFrameMs) {}

    // --- Audio: push from network thread, pop from audio callback ---

    void push_audio(const float* mono, size_t frames, uint64_t seq, utils::WallTimestamp sender_ts) {
        audio_.push(mono, frames, seq, sender_ts);
    }

    size_t pop_audio(float* out, size_t frames) {
        size_t got = audio_.pop(out, frames);
        const float vol = volume_.load(std::memory_order_relaxed);
        if (vol != 1.0f) {
            for (size_t i = 0; i < frames; ++i) {
                out[i] *= vol;
            }
        }
        return got;
    }

    // --- Video: push and pop from main thread only ---

    // sender_ts is stored but not yet used for A/V sync.
    void push_video(std::vector<uint8_t> rgba, int w, int h, utils::WallTimestamp sender_ts = {}) {
        video_.push(video_push_seq_++, VideoFrame{std::move(rgba), w, h}, sender_ts);
    }

    const VideoFrame* pop_video() {
        auto pkt = video_.pop();
        if (pkt) {
            current_video_ = std::move(*pkt);
        }
        return current_video_ ? &current_video_->data : nullptr;
    }

    // --- Stats ---

    struct Stats {
        struct Audio {
            bool primed = false;
            size_t queue_size = 0;
            size_t buffered_ms = 0;
            uint64_t push_count = 0;
            uint64_t drop_count = 0;
            uint64_t miss_count = 0;
            utils::WallTimestamp playback_ts{};
        };
        struct Video {
            bool primed = false;
            size_t queue_size = 0;
            size_t buffered_ms = 0;
            uint64_t push_count = 0;
            uint64_t drop_count = 0;
            uint64_t miss_count = 0;
        };
        Audio audio;
        Video video;
    };

    Stats stats() const {
        const auto a = audio_.stats();
        const auto v = video_.stats();
        return {
            .audio = {a.primed, a.queue_size, a.buffered_ms, a.push_count, a.drop_count, a.miss_count, a.playback_ts},
            .video = {v.primed, v.queue_size, v.buffered_ms, v.push_count, v.drop_count, v.miss_count},
        };
    }

    // --- Volume ---

    void set_volume(float v) { volume_.store(v, std::memory_order_relaxed); }
    float volume() const { return volume_.load(std::memory_order_relaxed); }

    // --- Status ---

    utils::WallTimestamp audio_playback_ts() const { return audio_.current_playback_ts(); }
    size_t audio_buffered_ms() const { return audio_.buffered_ms(); }
    size_t video_queue_size() const { return video_.queue_size(); }

    void reset() {
        audio_.reset();
        video_.reset();
        video_push_seq_ = 0;
        current_video_.reset();
    }

private:
    AudioJitter audio_;
    JitterBuffer<VideoFrame> video_;
    std::atomic<float> volume_{1.0f};

    uint64_t video_push_seq_ = 0;
    std::optional<JitterBuffer<VideoFrame>::Packet> current_video_;
};
