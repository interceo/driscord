#pragma once

#include "audio/audio_jitter.hpp"
#include "utils/jitter.hpp"
#include "utils/time.hpp"

#include <atomic>
#include <chrono>
#include <optional>
#include <vector>

class ScreenJitter {
public:
    struct VideoFrame {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
    };

    explicit ScreenJitter(int buffer_ms, int sample_rate = opus::kSampleRate)
        : audio_(static_cast<size_t>(buffer_ms), sample_rate), video_(buffer_ms, true /* rate_limit_pop */) {}

    // --- Audio ---

    void push_audio(const float* mono, size_t frames, uint64_t seq, utils::WallTimestamp sender_ts) {
        audio_.push(mono, frames, seq, sender_ts);
    }

    size_t pop_audio(float* out, size_t frames) {
        size_t got = audio_.pop(out, frames);
        const float vol = volume_.load();
        if (vol != 1.0f) {
            for (size_t i = 0; i < frames; ++i) {
                out[i] *= vol;
            }
        }
        return got;
    }

    // --- Video ---

    // frame_duration_us: real frame interval from the sender (1_000_000 / fps).
    void push_video(
        std::vector<uint8_t> rgba,
        int w,
        int h,
        uint32_t frame_duration_us,
        utils::WallTimestamp sender_ts = {}
    ) {
        if (frame_duration_us > 0) {
            video_.set_packet_duration(std::chrono::microseconds(frame_duration_us));
        }
        video_.push(video_push_seq_++, VideoFrame{std::move(rgba), w, h}, sender_ts);
    }

    // Returns a new frame or nullptr if nothing new is ready.
    const VideoFrame* pop_video() {
        auto pkt = video_.pop();
        if (!pkt) {
            return nullptr;
        }
        current_video_ = std::move(pkt->data);
        return &*current_video_;
    }

    // --- Stats ---

    struct Stats {
        struct Audio {
            size_t queue_size = 0;
            size_t buffered_ms = 0;
            uint64_t drop_count = 0;
            uint64_t miss_count = 0;
        };
        struct Video {
            size_t queue_size = 0;
            size_t buffered_ms = 0;
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
            .audio = {a.queue_size, a.buffered_ms, a.drop_count, a.miss_count},
            .video = {v.queue_size, v.buffered_ms, v.drop_count, v.miss_count},
        };
    }

    // --- Volume ---

    void set_volume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }

    // --- Status ---

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
    std::optional<VideoFrame> current_video_;
};
