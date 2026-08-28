#pragma once

// Marker-based A/V sync ground truth: the generator emits a video marker
// frame and mixes a chirp into the audio feed at the same synthetic capture
// instant; the receive side reports when each actually played out. The
// signed offset per event is audio minus video — negative means audio led.
// Catches SFU timestamp-rewriting bugs the stats-based playout-skew method
// cannot see.

#include "media_metrics.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace test_util {

struct AvSyncReport {
    size_t events = 0;
    size_t matched = 0;
    std::vector<double> offsets_ms;
    double abs_p50_ms = 0.0;
    double abs_p95_ms = 0.0;
    double abs_max_ms = 0.0;
    // Largest lead (audio earlier than video), in positive ms; the human ear
    // tolerates audio lag far better than lead (ITU-R BT.1359 asymmetry).
    double audio_lead_max_ms = 0.0;
};

class AvSyncCorrelator {
public:
    // A sync event: video frame `frame_index` and the chirp share this
    // capture instant.
    void register_event(uint32_t frame_index,
        std::chrono::steady_clock::time_point capture_time)
    {
        events_.push_back(Event { frame_index, capture_time });
    }

    // Every decoded video frame, with the capture time its own index maps to
    // (from the reference store). The first decoded frame at or shortly
    // after an event's index closes the video side; the correction term
    // rebases a slightly-later frame onto the event's capture instant.
    void on_video_frame(uint32_t frame_index,
        std::chrono::steady_clock::time_point frame_capture_time,
        std::chrono::steady_clock::time_point render_wall_time)
    {
        for (auto& event : events_) {
            if (event.video_wall.has_value()) {
                continue;
            }
            if (frame_index >= event.frame_index
                && frame_index <= event.frame_index + kMaxIndexSlack) {
                event.video_wall = render_wall_time
                    - (frame_capture_time - event.capture_time);
            }
        }
    }

    // A chirp playout detection; matched to the nearest pending event by
    // capture-time proximity.
    void on_chirp(std::chrono::steady_clock::time_point playout_wall_time)
    {
        Event* best = nullptr;
        std::chrono::milliseconds best_distance { 0 };
        for (auto& event : events_) {
            if (event.audio_wall.has_value()) {
                continue;
            }
            const auto distance
                = std::chrono::duration_cast<std::chrono::milliseconds>(
                    playout_wall_time > event.capture_time
                        ? playout_wall_time - event.capture_time
                        : event.capture_time - playout_wall_time);
            if (distance > kMatchWindow) {
                continue;
            }
            if (best == nullptr || distance < best_distance) {
                best = &event;
                best_distance = distance;
            }
        }
        if (best != nullptr) {
            best->audio_wall = playout_wall_time;
        }
    }

    [[nodiscard]] AvSyncReport report() const
    {
        AvSyncReport result;
        result.events = events_.size();
        std::vector<double> magnitudes;
        for (const auto& event : events_) {
            if (!event.video_wall || !event.audio_wall) {
                continue;
            }
            ++result.matched;
            const double offset_ms
                = std::chrono::duration_cast<std::chrono::microseconds>(
                      *event.audio_wall - *event.video_wall)
                    .count()
                / 1'000.0;
            result.offsets_ms.push_back(offset_ms);
            magnitudes.push_back(std::abs(offset_ms));
            if (offset_ms < 0.0) {
                result.audio_lead_max_ms
                    = std::max(result.audio_lead_max_ms, -offset_ms);
            }
        }
        if (!magnitudes.empty()) {
            result.abs_p50_ms = percentile(magnitudes, 50.0);
            result.abs_p95_ms = percentile(magnitudes, 95.0);
            result.abs_max_ms
                = *std::max_element(magnitudes.begin(), magnitudes.end());
        }
        return result;
    }

private:
    static constexpr uint32_t kMaxIndexSlack = 3;
    static constexpr std::chrono::milliseconds kMatchWindow { 500 };

    struct Event {
        uint32_t frame_index = 0;
        std::chrono::steady_clock::time_point capture_time;
        std::optional<std::chrono::steady_clock::time_point> video_wall;
        std::optional<std::chrono::steady_clock::time_point> audio_wall;
    };

    std::vector<Event> events_;
};

} // namespace test_util
