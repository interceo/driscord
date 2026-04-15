#include "driscord_core.hpp"
#include "audio/capture/system_audio_capture.hpp"
#include "config.hpp"
#include "utils/vector_view.hpp"
#include "video/capture/screen_capture.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

DriscordCore::DriscordCore()
    : audio_transport(transport)
    , video_transport(transport)
{
    transport.on_peer_joined([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_peer_joined_cb_) {
            on_peer_joined_cb_(id);
        }
    });
    transport.on_peer_left([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_peer_left_cb_) {
            on_peer_left_cb_(id);
        }
    });
    video_transport.on_new_streaming_peer([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_new_streaming_peer_cb_) {
            on_new_streaming_peer_cb_(id);
        }
    });
    video_transport.on_streaming_peer_removed([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_streaming_peer_removed_cb_) {
            on_streaming_peer_removed_cb_(id);
        }
    });
    transport.on_streaming_started([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_streaming_started_cb_) {
            on_streaming_started_cb_(id);
        }
    });
    transport.on_streaming_stopped([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_streaming_stopped_cb_) {
            on_streaming_stopped_cb_(id);
        }
    });
    transport.on_watch_started(
        [this](const std::string& id) { video_transport.add_subscriber(id); });
    transport.on_watch_stopped(
        [this](const std::string& id) { video_transport.remove_subscriber(id); });
    video_transport.on_peer_identity([this](const std::string& peer_id, const std::string& username) {
        std::scoped_lock lk(cb_mtx_);
        if (on_peer_identity_cb_) {
            on_peer_identity_cb_(peer_id, username);
        }
    });
}

// ---------------------------------------------------------------------------
// Callback setters
// ---------------------------------------------------------------------------

void DriscordCore::set_on_peer_joined(StringCb cb)
{
    std::scoped_lock lk(cb_mtx_);
    on_peer_joined_cb_ = std::move(cb);
}

void DriscordCore::set_on_peer_left(StringCb cb)
{
    std::scoped_lock lk(cb_mtx_);
    on_peer_left_cb_ = std::move(cb);
}

void DriscordCore::set_on_peer_identity(
    std::function<void(const std::string&, const std::string&)> cb)
{
    std::scoped_lock lk(cb_mtx_);
    on_peer_identity_cb_ = std::move(cb);
}

void DriscordCore::set_on_new_streaming_peer(StringCb cb)
{
    std::scoped_lock lk(cb_mtx_);
    on_new_streaming_peer_cb_ = std::move(cb);
}

void DriscordCore::set_on_streaming_peer_removed(StringCb cb)
{
    std::scoped_lock lk(cb_mtx_);
    on_streaming_peer_removed_cb_ = std::move(cb);
}

void DriscordCore::set_on_frame(FrameCb cb)
{
    std::scoped_lock lk(cb_mtx_);
    on_frame_cb_ = std::move(cb);
}

void DriscordCore::set_on_frame_removed(StringCb cb)
{
    std::scoped_lock lk(cb_mtx_);
    on_frame_removed_cb_ = std::move(cb);
}

void DriscordCore::set_on_streaming_started(StringCb cb)
{
    std::scoped_lock lk(cb_mtx_);
    on_streaming_started_cb_ = std::move(cb);
}

void DriscordCore::set_on_streaming_stopped(StringCb cb)
{
    std::scoped_lock lk(cb_mtx_);
    on_streaming_stopped_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// ScreenSession lifecycle
// ---------------------------------------------------------------------------

void DriscordCore::init_screen_session()
{
    screen_session.emplace(
        stream_defaults::kScreenBufferMs,
        stream_defaults::kVoiceJitterMs,
        std::chrono::milliseconds(stream_defaults::kMaxSyncGapMs),
        [this](const uint8_t* d, size_t l) { video_transport.send_video(d, l); },
        [this]() { video_transport.send_keyframe_request(); },
        [this](const uint8_t* d, size_t l) {
            audio_transport.send_screen_audio(d, l);
        });
    screen_session->set_system_audio_bitrate(stream_defaults::kSystemAudioBitrateKbps);
    screen_session->set_on_frame(
        [this](const std::string& pid, const uint8_t* rgba, int w, int h) {
            std::scoped_lock lk(cb_mtx_);
            if (on_frame_cb_) {
                on_frame_cb_(pid, rgba, w, h);
            }
        });
    screen_session->set_on_frame_removed([this](const std::string& pid) {
        on_video_peer_stream_ended(pid);
        std::scoped_lock lk(cb_mtx_);
        if (on_frame_removed_cb_) {
            on_frame_removed_cb_(pid);
        }
    });
    video_transport.set_video_sink(
        [this](const std::string& peer_id, const uint8_t* data, size_t len,
            uint64_t frame_id) {
            screen_session->push_video_packet(
                peer_id, utils::vector_view<const uint8_t> { data, len }, frame_id);
        },
        [this]() {
            if (screen_session->sharing()) {
                screen_session->force_keyframe();
            }
        });
}

void DriscordCore::deinit_screen_session()
{
    video_transport.clear_video_sink();
    screen_session.reset();
}

// ---------------------------------------------------------------------------
// Stream watching
// ---------------------------------------------------------------------------

void DriscordCore::join_stream(const std::string& peer_id)
{
    watched_peers_.insert(peer_id);
    screen_session->add_video_peer(peer_id);
    screen_session->add_audio_peer(peer_id);
    audio_transport.set_screen_audio_recv(
        peer_id, screen_session->audio_receiver(peer_id));
    audio_transport.add_screen_audio_to_mixer(peer_id);
    video_transport.add_watched_peer(peer_id);
    transport.send_watch_start();
    video_transport.send_keyframe_request();
}

void DriscordCore::leave_stream()
{
    transport.send_watch_stop();
    video_transport.clear_watched_peers();
    for (const auto& pid : watched_peers_) {
        audio_transport.remove_screen_audio_from_mixer(pid);
        audio_transport.unset_screen_audio_recv(pid);
        screen_session->remove_audio_peer(pid);
        screen_session->remove_video_peer(pid);
    }
    screen_session->reset();
    watched_peers_.clear();
}

void DriscordCore::on_video_peer_stream_ended(const std::string& peer_id)
{
    video_transport.remove_streaming_peer(peer_id);
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

std::string DriscordCore::peers_json() const
{
    auto ps = transport.peers();
    json arr = json::array();
    for (auto& p : ps) {
        arr.push_back({
            { "id", p.id },
            { "connected", p.primary_open },
            { "username", video_transport.peer_username(p.id) },
        });
    }
    return arr.dump();
}

void DriscordCore::set_local_username(const std::string& username)
{
    video_transport.set_local_username(username);
}

// ---------------------------------------------------------------------------
// Audio (cross-subsystem)
// ---------------------------------------------------------------------------

void DriscordCore::audio_set_screen_audio_receiver(const std::string& peer,
    bool has_screen)
{
    std::shared_ptr<AudioReceiver> recv;
    if (has_screen) {
        recv = screen_session->audio_receiver(peer);
    }
    audio_transport.set_screen_audio_recv(peer, std::move(recv));
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

void DriscordCore::video_set_watching(bool w)
{
    if (!w) {
        video_transport.clear_watched_peers();
    }
}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

std::string DriscordCore::capture_audio_list_targets_json() const
{
    const auto targets = SystemAudioCapture::list_sinks();
    json arr = json::array();
    for (const auto& it : targets) {
        arr.push_back({ { "id", it.id }, { "name", it.name } });
    }
    return arr.dump(-1, ' ', /*ensure_ascii=*/false,
        nlohmann::json::error_handler_t::replace);
}

std::string DriscordCore::capture_video_list_targets_json() const
{
    const auto targets = ScreenCapture::list_targets();
    json arr = json::array();
    for (const auto& it : targets) {
        arr.push_back({ { "type", it.type == ScreenCaptureTarget::Monitor ? 0 : 1 },
            { "id", it.id },
            { "name", it.name },
            { "width", it.width },
            { "height", it.height },
            { "x", it.x },
            { "y", it.y } });
    }
    return arr.dump(-1, ' ', /*ensure_ascii=*/false,
        nlohmann::json::error_handler_t::replace);
}

std::vector<uint8_t> DriscordCore::capture_grab_thumbnail(
    const std::string& target_json,
    int max_w,
    int max_h)
{
    const auto target = ScreenCaptureTarget::from_json(json::parse(target_json));
    const auto frame = ScreenCapture::grab_thumbnail(target, max_w, max_h);
    if (frame.data.empty()) {
        return { };
    }
    return frame.to_rgba();
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

utils::Expected<void, VideoError> DriscordCore::screen_start_sharing(
    const std::string& target_json,
    int max_w,
    int max_h,
    int fps,
    bool share_audio)
{
    nlohmann::json j;
    try {
        j = json::parse(target_json);
    } catch (const std::exception&) {
        return utils::Unexpected(VideoError::CaptureStartFailed);
    }
    const auto target = ScreenCaptureTarget::from_json(j);
    auto r = screen_session->start_sharing(target, max_w, max_h, fps, share_audio);
    if (r) {
        transport.send_streaming_start();
    }
    return r;
}

void DriscordCore::screen_stop_sharing()
{
    screen_session->stop_sharing();
    video_transport.send_stop_stream();
    transport.send_streaming_stop();
}

void DriscordCore::screen_set_stream_volume(const std::string& peer, float vol)
{
    audio_transport.set_screen_audio_peer_volume(peer, vol);
}

float DriscordCore::screen_stream_volume() const
{
    return audio_transport.screen_audio_peer_volume(
        screen_session->active_peer());
}
