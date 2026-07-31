#include "screen_session.hpp"

#include <chrono>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace {

// The UI reads these; refreshing them at the render rate would lock the
// receiver maps 60 times a second for numbers nobody can read that fast.
constexpr auto kStatsRefreshInterval = std::chrono::milliseconds(500);

} // namespace

ScreenSession::ScreenSession(SendCb send_video,
    std::function<void()> on_keyframe_req,
    SendCb send_screen_audio)
    : send_video_(std::move(send_video))
    , send_screen_audio_(std::move(send_screen_audio))
{
    receiver_.set_keyframe_callback(std::move(on_keyframe_req));
}

utils::Expected<void, VideoError> ScreenSession::start_sharing(const ScreenCaptureTarget& target,
    const size_t max_w,
    const size_t max_h,
    const size_t fps,
    bool share_audio)
{
    return sender_.start_sharing(target, max_w, max_h, fps,
        share_audio, send_video_, send_screen_audio_);
}

void ScreenSession::stop_sharing()
{
    sender_.stop_sharing();
}

void ScreenSession::push_video_packet(
    const std::string& peer_id,
    const utils::vector_view<const uint8_t> data,
    uint64_t frame_id)
{
    receiver_.push_video_packet(peer_id, data, frame_id);
}

void ScreenSession::push_audio_packet(
    const std::string& peer_id,
    const utils::vector_view<const uint8_t> data)
{
    receiver_.push_audio_packet(peer_id,
        data); // peer_id routed inside AudioReceiver
}

void ScreenSession::update()
{
    // No drift correction here any more. Both halves of the screen share read
    // their playout deadlines from the same MediaClock, so they cannot drift
    // apart in the first place — the eviction machinery that used to fast
    // forward whichever stream had fallen behind has nothing left to fix.

    const auto now = Clock::now();
    if (now - last_stats_refresh_ >= kStatsRefreshInterval) {
        cached_video_stats_ = receiver_.video_stats();
        cached_audio_stats_ = receiver_.audio_stats();
        last_stats_refresh_ = now;
    }

    std::unordered_set<std::string> seen_this_tick;
    receiver_.update([&](const VideoReceiver::Frame& frame) {
        const std::string& peer = frame.peer_id;
        if (peer.empty()) {
            return;
        }
        seen_this_tick.insert(peer);
        last_w_ = frame.width;
        last_h_ = frame.height;
        std::scoped_lock lk(cb_mutex_);
        if (on_frame_cb_) {
            on_frame_cb_(peer, frame.rgba.data(), frame.width, frame.height);
        }
    });

    // Fire on_frame_removed_cb_ for peers that have gone stale.
    const auto active = receiver_.active_peers();
    for (auto it = last_peers_.begin(); it != last_peers_.end();) {
        if (active.count(*it) == 0) {
            std::scoped_lock lk(cb_mutex_);
            if (on_frame_removed_cb_) {
                on_frame_removed_cb_(*it);
            }
            it = last_peers_.erase(it);
        } else {
            ++it;
        }
    }
    last_peers_.insert(seen_this_tick.begin(), seen_this_tick.end());
}

void ScreenSession::set_on_frame(OnFrameCb cb)
{
    std::scoped_lock lk(cb_mutex_);
    on_frame_cb_ = std::move(cb);
}

void ScreenSession::set_on_frame_removed(OnRemovedCb cb)
{
    std::scoped_lock lk(cb_mutex_);
    on_frame_removed_cb_ = std::move(cb);
}

void ScreenSession::reset()
{
    receiver_.reset();
    last_peers_.clear();
}

void ScreenSession::reset_audio()
{
    receiver_.reset_audio();
}

std::string ScreenSession::stats_json() const
{
    auto vs = video_stats();
    auto as = audio_stats();
    nlohmann::json j = {
        { "width", last_w_ },
        { "height", last_h_ },
        { "measuredKbps", vs.measured_kbps },
        { "video",
            { { "queue", vs.queue_size },
                { "drops", vs.drop_count },
                { "late", vs.late_count },
                { "targetDelayMs", vs.target_delay_ms },
                { "packetsReceived", vs.packets_received },
                { "decodeFailures", vs.decode_failures },
                { "keyframeRequests", vs.keyframe_requests } } },
        { "audio",
            { { "queue", as.queue_size },
                { "drops", as.drop_count },
                { "conceals", as.conceal_count },
                { "fecRecovered", as.fec_count },
                { "underruns", as.underrun_count },
                { "stretches", as.stretch_count },
                { "targetDelayMs", as.target_delay_ms },
                { "actualDelayMs", as.actual_delay_ms },
                { "packetsReceived", as.packets_received },
                { "decodeErrors", as.decode_errors } } },
    };
    return j.dump();
}
