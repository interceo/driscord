#include "webrtc/google_webrtc_screen_stats.hpp"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace driscord::media {
namespace {

    using json = nlohmann::json;

    double average_ms(double total_seconds, uint64_t count)
    {
        return count == 0 ? -1.0 : total_seconds * 1000.0 / count;
    }

}

void ScreenStatsTracker::set_binding(
    std::string mid, std::optional<std::string> peer_id)
{
    const auto old = bindings_.find(mid);
    const bool changed = old == bindings_.end()
        ? peer_id.has_value()
        : !peer_id || old->second != *peer_id;
    if (!changed) {
        return;
    }

    baselines_.erase(mid);
    if (old != bindings_.end()) {
        cache_by_peer_.erase(old->second);
    }
    if (peer_id) {
        cache_by_peer_.erase(*peer_id);
        bindings_[std::move(mid)] = std::move(*peer_id);
    } else {
        bindings_.erase(old);
    }
}

void ScreenStatsTracker::set_watched(std::string peer_id, bool watched)
{
    if (watched) {
        watched_peers_.insert(std::move(peer_id));
    } else {
        watched_peers_.erase(peer_id);
        cache_by_peer_.erase(peer_id);
    }
}

void ScreenStatsTracker::clear_watched()
{
    watched_peers_.clear();
    cache_by_peer_.clear();
}

void ScreenStatsTracker::remove_peer(std::string_view peer_id)
{
    const std::string peer(peer_id);
    watched_peers_.erase(peer);
    cache_by_peer_.erase(peer);
    for (auto binding = bindings_.begin(); binding != bindings_.end();) {
        if (binding->second == peer) {
            baselines_.erase(binding->first);
            binding = bindings_.erase(binding);
        } else {
            ++binding;
        }
    }
}

void ScreenStatsTracker::reset_session()
{
    bindings_.clear();
    cache_by_peer_.clear();
    baselines_.clear();
    request_in_flight_ = false;
    ++session_generation_;
}

ScreenStatsTracker::Poll ScreenStatsTracker::poll(
    std::string_view peer_id, bool session_available)
{
    const auto cached = cache_by_peer_.find(std::string(peer_id));
    Poll result {
        .json = cached == cache_by_peer_.end() ? Cache { }.json
                                               : cached->second.json,
        .start_request = session_available && !request_in_flight_,
        .session_generation = session_generation_,
    };
    if (result.start_request) {
        request_in_flight_ = true;
    }
    return result;
}

void ScreenStatsTracker::request_failed(uint64_t session_generation)
{
    if (session_generation == session_generation_) {
        request_in_flight_ = false;
    }
}

void ScreenStatsTracker::consume(ScreenSessionStats stats,
    uint64_t session_generation,
    TimePoint now)
{
    if (session_generation != session_generation_) {
        return;
    }
    for (const auto& inbound : stats.inbound) {
        if (bindings_.contains(inbound.mid)) {
            baselines_.try_emplace(inbound.mid, inbound);
        }
    }
    for (const auto& peer : watched_peers_) {
        update_peer(peer, stats, now, cache_by_peer_[peer]);
    }
    request_in_flight_ = false;
}

void ScreenStatsTracker::update_peer(std::string_view peer_id,
    const ScreenSessionStats& stats,
    TimePoint now,
    Cache& cache) const
{
    uint64_t video_packets = 0;
    uint64_t video_bytes = 0;
    uint64_t video_lost = 0;
    uint64_t video_frames_decoded = 0;
    uint64_t video_frames_dropped = 0;
    uint64_t video_keyframes = 0;
    uint64_t video_emitted = 0;
    double video_delay = 0.0;
    double video_target_delay = 0.0;
    uint64_t audio_packets = 0;
    uint64_t audio_bytes = 0;
    uint64_t audio_lost = 0;
    uint64_t audio_concealed = 0;
    uint64_t audio_emitted = 0;
    double audio_delay = 0.0;
    double audio_target_delay = 0.0;

    for (const auto& inbound : stats.inbound) {
        const auto binding = bindings_.find(inbound.mid);
        if (binding == bindings_.end() || binding->second != peer_id) {
            continue;
        }
        const auto found = baselines_.find(inbound.mid);
        const ScreenInboundRtpStats* baseline
            = found == baselines_.end() ? nullptr : &found->second;
        const auto delta = [baseline](auto current, auto member) {
            if (!baseline) {
                return current;
            }
            const auto initial = baseline->*member;
            return current >= initial ? current - initial
                                      : decltype(current) { 0 };
        };
        const auto lost_delta = baseline
            ? std::max<int64_t>(
                  0, inbound.packets_lost - baseline->packets_lost)
            : std::max<int64_t>(0, inbound.packets_lost);

        if (inbound.video) {
            video_packets += delta(inbound.packets_received,
                &ScreenInboundRtpStats::packets_received);
            video_bytes += delta(
                inbound.bytes_received, &ScreenInboundRtpStats::bytes_received);
            video_lost += static_cast<uint64_t>(lost_delta);
            video_frames_decoded += delta(
                inbound.frames_decoded, &ScreenInboundRtpStats::frames_decoded);
            video_frames_dropped += delta(
                inbound.frames_dropped, &ScreenInboundRtpStats::frames_dropped);
            video_keyframes += delta(inbound.key_frames_decoded,
                &ScreenInboundRtpStats::key_frames_decoded);
            video_emitted += delta(inbound.jitter_buffer_emitted_count,
                &ScreenInboundRtpStats::jitter_buffer_emitted_count);
            video_delay += delta(inbound.jitter_buffer_delay_seconds,
                &ScreenInboundRtpStats::jitter_buffer_delay_seconds);
            video_target_delay += delta(
                inbound.jitter_buffer_target_delay_seconds,
                &ScreenInboundRtpStats::jitter_buffer_target_delay_seconds);
        } else {
            audio_packets += delta(inbound.packets_received,
                &ScreenInboundRtpStats::packets_received);
            audio_bytes += delta(
                inbound.bytes_received, &ScreenInboundRtpStats::bytes_received);
            audio_lost += static_cast<uint64_t>(lost_delta);
            audio_concealed += delta(inbound.concealed_samples,
                &ScreenInboundRtpStats::concealed_samples);
            audio_emitted += delta(inbound.jitter_buffer_emitted_count,
                &ScreenInboundRtpStats::jitter_buffer_emitted_count);
            audio_delay += delta(inbound.jitter_buffer_delay_seconds,
                &ScreenInboundRtpStats::jitter_buffer_delay_seconds);
            audio_target_delay += delta(
                inbound.jitter_buffer_target_delay_seconds,
                &ScreenInboundRtpStats::jitter_buffer_target_delay_seconds);
        }
    }

    const uint64_t received_bytes = video_bytes + audio_bytes;
    int measured_kbps = 0;
    if (cache.updated_at.time_since_epoch().count() != 0
        && received_bytes >= cache.received_bytes) {
        const double seconds
            = std::chrono::duration<double>(now - cache.updated_at).count();
        if (seconds > 0.0) {
            measured_kbps = static_cast<int>(std::llround(
                static_cast<double>(received_bytes - cache.received_bytes)
                * 8.0 / seconds / 1000.0));
        }
    }
    cache.json = json {
        { "measuredKbps", measured_kbps },
        { "video",
            {
                { "packetsReceived", video_packets },
                { "bytesReceived", video_bytes },
                { "packetsLost", video_lost },
                { "framesDecoded", video_frames_decoded },
                { "framesDropped", video_frames_dropped },
                { "keyFramesDecoded", video_keyframes },
                { "actualDelayMs", average_ms(video_delay, video_emitted) },
                { "targetDelayMs",
                    average_ms(video_target_delay, video_emitted) },
            } },
        { "audio",
            {
                { "packetsReceived", audio_packets },
                { "bytesReceived", audio_bytes },
                { "packetsLost", audio_lost },
                { "concealedSamples", audio_concealed },
                { "actualDelayMs", average_ms(audio_delay, audio_emitted) },
                { "targetDelayMs",
                    average_ms(audio_target_delay, audio_emitted) },
            } },
        { "outbound",
            {
                { "videoPackets", 0 },
                { "videoBytes", 0 },
                { "audioPackets", 0 },
                { "audioBytes", 0 },
            } },
    }
                     .dump();
    cache.received_bytes = received_bytes;
    cache.updated_at = now;
}

}
