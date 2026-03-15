#pragma once

#include "audio/audio_jitter.hpp"
#include "log.hpp"
#include "utils/time.hpp"

#include <atomic>
#include <deque>
#include <vector>

class ScreenStreamJitter {
public:
    struct VideoFrame {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        utils::WallTimestamp sender_ts{};
    };

    ScreenStreamJitter(int buffer_ms, int max_sync_gap_ms, int sample_rate = 48000)
        : audio_(buffer_ms, sample_rate),
          buffer_ms_(static_cast<uint32_t>(buffer_ms)),
          max_sync_gap_ms_(static_cast<uint32_t>(max_sync_gap_ms)),
          max_queue_(static_cast<size_t>(buffer_ms) * 120 / 1000 + 30) {}

    // --- Audio: push from network thread, pop from audio callback (SPSC safe) ---

    void push_audio(const float* mono, size_t frames, uint64_t seq, utils::WallTimestamp sender_ts) {
        ++audio_push_count_;
        audio_.push(mono, frames, seq, sender_ts);
    }

    size_t pop_audio(float* out, size_t frames) {
        size_t got = audio_.pop(out, frames);
        float vol = volume_.load(std::memory_order_relaxed);
        if (vol != 1.0f) {
            for (size_t i = 0; i < frames; ++i) {
                out[i] *= vol;
            }
        }
        ++audio_pop_count_;
        if (audio_pop_count_ % 500 == 0) {
            LOG_INFO()
                << "[stream-jitter-audio] pop=" << audio_pop_count_ << " push=" << audio_push_count_ << " got=" << got
                << "/" << frames << " buffered=" << audio_.buffered_ms() << "ms"
                << " playback_ts=" << utils::WallToMs(audio_.current_playback_ts());
        }
        return got;
    }

    // --- Video: push and pop from main thread only ---

    void push_video(std::vector<uint8_t> rgba, int w, int h, utils::WallTimestamp sender_ts) {
        VideoFrame frame{std::move(rgba), w, h, sender_ts};
        auto it = video_queue_.end();
        while (it != video_queue_.begin()) {
            auto prev = std::prev(it);
            if (sender_ts >= prev->sender_ts) {
                break;
            }
            it = prev;
        }
        video_queue_.insert(it, std::move(frame));
        while (video_queue_.size() > max_queue_) {
            video_queue_.pop_front();
        }
    }

    const VideoFrame* pop_video() {
        if (video_queue_.empty()) {
            return has_current_ ? &current_ : nullptr;
        }

        const utils::WallTimestamp audio_ts = audio_.current_playback_ts();
        const bool has_clock = (audio_ts != utils::WallTimestamp{});

        ++video_pop_count_;
        if (video_pop_count_ % 60 == 0) {
            LOG_INFO()
                << "[stream-jitter-sync] video_pop=" << video_pop_count_ << " primed=" << video_primed_ << " has_clock="
                << has_clock << " audio_ts=" << utils::WallToMs(audio_ts) << " queue=" << video_queue_.size()
                << " front_ts=" << utils::WallToMs(video_queue_.front().sender_ts) << " back_ts="
                << utils::WallToMs(video_queue_.back().sender_ts) << " audio_buf=" << audio_.buffered_ms() << "ms"
                << " audio_push=" << audio_push_count_;
        }

        if (!video_primed_) {
            if (!has_clock) {
                current_ = std::move(video_queue_.back());
                has_current_ = true;
                video_queue_.clear();
                return &current_;
            }
            const int64_t span_ms = utils::WallElapsedMs(video_queue_.front().sender_ts, video_queue_.back().sender_ts);
            if (span_ms < buffer_ms_) {
                return has_current_ ? &current_ : nullptr;
            }
            video_primed_ = true;
            LOG_INFO() << "[stream-jitter] video primed, queue=" << video_queue_.size() << " span=" << span_ms << "ms";
        }

        if (!has_clock) {
            LOG_INFO() << "[stream-jitter] clock lost after priming! audio_ts=0 queue=" << video_queue_.size();
            current_ = std::move(video_queue_.back());
            has_current_ = true;
            video_queue_.clear();
            return &current_;
        }

        const utils::WallTimestamp front_ts = video_queue_.front().sender_ts;
        const int64_t gap_ms = utils::WallElapsedMs(audio_ts, front_ts);

        if (gap_ms > static_cast<int64_t>(max_sync_gap_ms_)) {
            current_ = std::move(video_queue_.back());
            has_current_ = true;
            LOG_INFO()
                << "[stream-jitter] RESYNC: gap=" << gap_ms << "ms"
                << " force display ts=" << utils::WallToMs(current_.sender_ts)
                << " audio_ts=" << utils::WallToMs(audio_ts) << " front_ts=" << utils::WallToMs(front_ts)
                << " queue=" << video_queue_.size() << " audio_buf=" << audio_.buffered_ms() << "ms";
            audio_.re_anchor(current_.sender_ts);
            video_queue_.clear();
            return &current_;
        }

        int last_ready = -1;
        for (int i = 0; i < static_cast<int>(video_queue_.size()); ++i) {
            if (video_queue_[i].sender_ts <= audio_ts) {
                last_ready = i;
            } else {
                break;
            }
        }

        if (last_ready >= 0) {
            current_ = std::move(video_queue_[last_ready]);
            has_current_ = true;
            video_queue_.erase(video_queue_.begin(), video_queue_.begin() + last_ready + 1);
            return &current_;
        }

        return has_current_ ? &current_ : nullptr;
    }

    // --- Volume ---

    void set_volume(float v) { volume_.store(v, std::memory_order_relaxed); }
    float volume() const { return volume_.load(std::memory_order_relaxed); }

    // --- Status ---

    utils::WallTimestamp audio_playback_ts() const { return audio_.current_playback_ts(); }
    size_t audio_buffered_ms() const { return audio_.buffered_ms(); }
    size_t video_queue_size() const { return video_queue_.size(); }

    void reset() {
        audio_.reset();
        video_queue_.clear();
        video_primed_ = false;
        has_current_ = false;
        current_ = {};
    }

private:
    AudioJitter audio_;
    uint32_t buffer_ms_;
    uint32_t max_sync_gap_ms_;
    size_t max_queue_;

    std::deque<VideoFrame> video_queue_;
    bool video_primed_ = false;

    VideoFrame current_;
    bool has_current_ = false;

    std::atomic<float> volume_{1.0f};

    uint64_t audio_push_count_ = 0;
    uint64_t audio_pop_count_ = 0;
    uint64_t video_pop_count_ = 0;
};
