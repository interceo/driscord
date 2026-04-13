#include "screen_session.hpp"

#include <chrono>
#include <unordered_set>

#include <nlohmann/json.hpp>

ScreenSession::ScreenSession(int buf_ms,
    int audio_jitter_ms,
    utils::Duration max_sync,
    SendCb send_video,
    std::function<void()> on_keyframe_req,
    SendCb send_screen_audio)
    : receiver_(
          buf_ms,
          static_cast<int>(
              std::chrono::duration_cast<std::chrono::milliseconds>(max_sync)
                  .count()),
          audio_jitter_ms)
    , send_video_(std::move(send_video))
    , send_screen_audio_(std::move(send_screen_audio))
    , max_sync_(max_sync)
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
    if (max_sync_ > utils::Duration::zero()) {
        receiver_.evict_old(max_sync_);

        if (receiver_.video_primed() && receiver_.audio_primed()) {
            const auto v_ts = receiver_.video_front_effective_ts();
            const auto a_ts = receiver_.audio_front_effective_ts();
            if (v_ts && a_ts) {
                // 3.3: clock-skew correction.
                // If A and V come from different senders their wall clocks may
                // diverge over time.  The difference of median OWDs approximates
                // the relative drift: a positive value means audio sender's clock
                // is behind video sender's (or audio has higher latency).
                // We subtract this bias so the eviction threshold is not polluted
                // by hardware crystal differences.
                const int64_t v_ow = receiver_.video_median_ow_delay_ms();
                const int64_t a_ow = receiver_.audio_median_ow_delay_ms();
                const auto skew_correction_ms = (v_ow >= 0 && a_ow >= 0)
                    ? std::chrono::milliseconds(a_ow - v_ow)
                    : std::chrono::milliseconds(0);

                const auto raw_drift = std::chrono::duration_cast<std::chrono::milliseconds>(*a_ts - *v_ts);
                const auto drift_ms = raw_drift - skew_correction_ms;
                const auto frame_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    receiver_.video_frame_duration());
                const auto max_sync_ms = std::chrono::duration_cast<std::chrono::milliseconds>(max_sync_);
                const auto threshold = max_sync_ms + frame_ms;

                if (drift_ms > threshold) {
                    // Video is behind audio: fast-forward video.
                    receiver_.evict_video_before(*a_ts - frame_ms);
                } else if (-drift_ms > threshold) {
                    // 3.2: Audio is behind video: fast-forward audio.
                    receiver_.evict_audio_before(*v_ts - frame_ms);
                }
            }
        }
    }

    const auto now = Clock::now();
    if (now - last_stats_refresh_ >= std::chrono::milliseconds(500)) {
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
                { "misses", vs.miss_count },
                { "packetsReceived", vs.packets_received },
                { "decodeFailures", vs.decode_failures },
                { "keyframeRequests", vs.keyframe_requests } } },
        { "audio",
            { { "queue", as.queue_size },
                { "drops", as.drop_count },
                { "misses", as.miss_count },
                { "packetsReceived", as.packets_received },
                { "decodeErrors", as.decode_errors } } },
    };
    return j.dump();
}
