#include "screen_session.hpp"

#include "utils/log.hpp"

#include <chrono>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <span>

namespace {

// The UI reads these; refreshing them at the render rate would lock the
// receiver maps 60 times a second for numbers nobody can read that fast.
constexpr auto kStatsRefreshInterval = std::chrono::milliseconds(500);
constexpr auto kStatsLogInterval = std::chrono::seconds(1);

int64_t av_skew_ms(const VideoReceiver::Stats& vs,
    const AudioReceiver::Stats& as)
{
    if (vs.last_shown_ts_us == 0 || as.playout_ts_us == 0) {
        return 0;
    }
    return (as.playout_ts_us - vs.last_shown_ts_us) / 1000;
}

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
    const std::span<const uint8_t> data,
    uint64_t frame_id)
{
    receiver_.push_video_packet(peer_id, data, frame_id);
}

void ScreenSession::push_audio_packet(
    const std::string& peer_id,
    const std::span<const uint8_t> data)
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
    if (now - last_stats_log_ >= kStatsLogInterval) {
        const nlohmann::json log = {
            { "event", "stream_stats" },
            { "activePeer", receiver_.active_peer() },
            { "width", last_w_ },
            { "height", last_h_ },
            { "measuredKbps", cached_video_stats_.measured_kbps },
            { "avSkewMs", av_skew_ms(cached_video_stats_, cached_audio_stats_) },
            { "video",
                { { "queue", cached_video_stats_.queue_size },
                    { "drops", cached_video_stats_.drop_count },
                    { "late", cached_video_stats_.late_count },
                    { "targetDelayMs", cached_video_stats_.target_delay_ms },
                    { "p50DelayMs", cached_video_stats_.p50_delay_ms },
                    { "p95DelayMs", cached_video_stats_.p95_delay_ms },
                    { "p99DelayMs", cached_video_stats_.p99_delay_ms },
                    { "delaySamples", cached_video_stats_.delay_samples },
                    { "lastShownTsUs", cached_video_stats_.last_shown_ts_us },
                    { "packetsReceived", cached_video_stats_.packets_received },
                    { "decodeFailures", cached_video_stats_.decode_failures },
                    { "keyframeRequests", cached_video_stats_.keyframe_requests } } },
            { "audio",
                { { "queue", cached_audio_stats_.queue_size },
                    { "drops", cached_audio_stats_.drop_count },
                    { "conceals", cached_audio_stats_.conceal_count },
                    { "fecRecovered", cached_audio_stats_.fec_count },
                    { "underruns", cached_audio_stats_.underrun_count },
                    { "stretches", cached_audio_stats_.stretch_count },
                    { "resyncs", cached_audio_stats_.resync_count },
                    { "targetDelayMs", cached_audio_stats_.target_delay_ms },
                    { "actualDelayMs", cached_audio_stats_.actual_delay_ms },
                    { "p50DelayMs", cached_audio_stats_.p50_delay_ms },
                    { "p95DelayMs", cached_audio_stats_.p95_delay_ms },
                    { "p99DelayMs", cached_audio_stats_.p99_delay_ms },
                    { "delaySamples", cached_audio_stats_.delay_samples },
                    { "playoutTsUs", cached_audio_stats_.playout_ts_us },
                    { "packetsReceived", cached_audio_stats_.packets_received },
                    { "decodeErrors", cached_audio_stats_.decode_errors } } },
        };
        LOG_INFO() << "stream_stats " << log.dump();
        last_stats_log_ = now;
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
        { "avSkewMs", av_skew_ms(vs, as) },
        { "video",
            { { "queue", vs.queue_size },
                { "drops", vs.drop_count },
                { "late", vs.late_count },
                { "targetDelayMs", vs.target_delay_ms },
                { "p50DelayMs", vs.p50_delay_ms },
                { "p95DelayMs", vs.p95_delay_ms },
                { "p99DelayMs", vs.p99_delay_ms },
                { "delaySamples", vs.delay_samples },
                { "lastShownTsUs", vs.last_shown_ts_us },
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
                { "p50DelayMs", as.p50_delay_ms },
                { "p95DelayMs", as.p95_delay_ms },
                { "p99DelayMs", as.p99_delay_ms },
                { "delaySamples", as.delay_samples },
                { "packetsReceived", as.packets_received },
                { "decodeErrors", as.decode_errors } } },
    };
    return j.dump();
}
