#include "driscord_core.hpp"

#include "audio/capture/system_audio_capture.hpp"
#include "webrtc/google_webrtc_client.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

DriscordCore::DriscordCore()
{
    media_ = std::make_unique<GoogleWebRtcClient>(transport,
        GoogleWebRtcClient::Callbacks {
            .on_frame = [this](const std::string& peer, const uint8_t* rgba,
                            int width, int height) { emit_frame(peer, rgba, width, height); },
            .on_frame_removed = [this](const std::string& peer) { emit_frame_removed(peer); },
        });

    transport.on_peer_joined([this](const std::string& id) {
        std::scoped_lock lock(cb_mtx_);
        if (on_peer_joined_cb_) {
            on_peer_joined_cb_(id);
        }
    });
    transport.on_peer_left([this](const std::string& id) {
        media_->remove_peer(id);
        std::scoped_lock lock(cb_mtx_);
        if (on_peer_left_cb_) {
            on_peer_left_cb_(id);
        }
    });
    transport.on_streaming_started([this](const std::string& id) {
        std::scoped_lock lock(cb_mtx_);
        if (on_streaming_started_cb_) {
            on_streaming_started_cb_(id);
        }
    });
    transport.on_streaming_stopped([this](const std::string& id) {
        media_->remove_peer(id);
        std::scoped_lock lock(cb_mtx_);
        if (on_streaming_stopped_cb_) {
            on_streaming_stopped_cb_(id);
        }
    });
    transport.on_watch_rejected([this](const std::string& id,
                                    signaling::WatchRejectReason reason) {
        std::scoped_lock lock(cb_mtx_);
        if (on_watch_rejected_cb_) {
            on_watch_rejected_cb_(id, reason);
        }
    });
    transport.on_peer_identity(
        [this](const std::string& peer, const std::string& username) {
            std::scoped_lock lock(cb_mtx_);
            if (on_peer_identity_cb_) {
                on_peer_identity_cb_(peer, username);
            }
        });
}

DriscordCore::~DriscordCore()
{
    transport.disconnect();
    media_.reset();
}

void DriscordCore::init_screen_session()
{
    media_->init_screen();
}

void DriscordCore::deinit_screen_session()
{
    media_->deinit_screen();
}

void DriscordCore::join_stream(const std::string& peer_id)
{
    media_->join_stream(peer_id);
}

void DriscordCore::leave_stream()
{
    media_->leave_streams();
}

void DriscordCore::leave_stream(const std::string& peer_id)
{
    media_->leave_stream(peer_id);
}

void DriscordCore::set_on_peer_joined(StringCb cb)
{
    std::scoped_lock lock(cb_mtx_);
    on_peer_joined_cb_ = std::move(cb);
}

void DriscordCore::set_on_peer_left(StringCb cb)
{
    std::scoped_lock lock(cb_mtx_);
    on_peer_left_cb_ = std::move(cb);
}

void DriscordCore::set_on_peer_identity(
    std::function<void(const std::string&, const std::string&)> cb)
{
    std::scoped_lock lock(cb_mtx_);
    on_peer_identity_cb_ = std::move(cb);
}

void DriscordCore::set_on_frame(FrameCb cb)
{
    std::scoped_lock lock(cb_mtx_);
    on_frame_cb_ = std::move(cb);
}

void DriscordCore::set_on_frame_removed(StringCb cb)
{
    std::scoped_lock lock(cb_mtx_);
    on_frame_removed_cb_ = std::move(cb);
}

void DriscordCore::set_on_streaming_started(StringCb cb)
{
    std::scoped_lock lock(cb_mtx_);
    on_streaming_started_cb_ = std::move(cb);
}

void DriscordCore::set_on_streaming_stopped(StringCb cb)
{
    std::scoped_lock lock(cb_mtx_);
    on_streaming_stopped_cb_ = std::move(cb);
}

void DriscordCore::set_on_watch_rejected(WatchRejectedCb cb)
{
    std::scoped_lock lock(cb_mtx_);
    on_watch_rejected_cb_ = std::move(cb);
}

void DriscordCore::emit_frame(const std::string& peer,
    const uint8_t* rgba,
    int width,
    int height)
{
    std::scoped_lock lock(cb_mtx_);
    if (on_frame_cb_) {
        on_frame_cb_(peer, rgba, width, height);
    }
}

void DriscordCore::emit_frame_removed(const std::string& peer)
{
    std::scoped_lock lock(cb_mtx_);
    if (on_frame_removed_cb_) {
        on_frame_removed_cb_(peer);
    }
}

std::string DriscordCore::peers_json() const
{
    json result = json::array();
    for (const auto& peer : transport.peers()) {
        result.push_back({
            { "id", peer.id },
            { "username", transport.peer_username(peer.id) },
        });
    }
    return result.dump();
}

void DriscordCore::voice_start()
{
    media_->start_voice();
}

void DriscordCore::voice_stop()
{
    media_->stop_voice();
}

void DriscordCore::voice_set_muted(bool muted)
{
    media_->set_muted(muted);
}

bool DriscordCore::voice_muted() const
{
    return media_->muted();
}

void DriscordCore::audio_set_deafened(bool deafened)
{
    media_->set_deafened(deafened);
}

bool DriscordCore::audio_deafened() const
{
    return media_->deafened();
}

void DriscordCore::audio_set_master_volume(float volume)
{
    media_->set_master_volume(volume);
}

float DriscordCore::audio_master_volume() const
{
    return media_->master_volume();
}

float DriscordCore::audio_input_level() const
{
    return 0.0f;
}

float DriscordCore::audio_output_level() const
{
    return 0.0f;
}

void DriscordCore::audio_set_noise_gate(float)
{
    // Google WebRTC's native audio-processing module owns voice detection,
    // noise suppression and gain control. A second amplitude gate would cut
    // already-processed speech and is intentionally not reintroduced.
}

std::string DriscordCore::audio_input_devices_json() const
{
    json result = json::array();
    for (const auto& device : media_->input_devices()) {
        result.push_back({ { "id", device.id }, { "name", device.name } });
    }
    return result.dump();
}

std::string DriscordCore::audio_output_devices_json() const
{
    json result = json::array();
    for (const auto& device : media_->output_devices()) {
        result.push_back({ { "id", device.id }, { "name", device.name } });
    }
    return result.dump();
}

bool DriscordCore::audio_set_input_device(const std::string& id)
{
    return media_->set_input_device(id);
}

bool DriscordCore::audio_set_output_device(const std::string& id)
{
    return media_->set_output_device(id);
}

void DriscordCore::audio_set_peer_volume(
    const std::string& peer, float volume)
{
    media_->set_peer_volume(peer, volume);
}

float DriscordCore::audio_peer_volume(const std::string& peer) const
{
    return media_->peer_volume(peer);
}

void DriscordCore::audio_set_peer_muted(
    const std::string& peer, bool muted)
{
    media_->set_peer_muted(peer, muted);
}

bool DriscordCore::audio_peer_muted(const std::string& peer) const
{
    return media_->peer_muted(peer);
}

bool DriscordCore::video_watching() const
{
    return media_->watching();
}

std::string DriscordCore::capture_audio_list_targets_json() const
{
    json result = json::array();
    for (const auto& target : SystemAudioCapture::list_sinks()) {
        result.push_back({ { "id", target.id }, { "name", target.name } });
    }
    return result.dump(-1, ' ', false,
        nlohmann::json::error_handler_t::replace);
}

std::string DriscordCore::capture_video_list_targets_json() const
{
    return media_->video_targets_json();
}

DriscordCore::ThumbnailResult DriscordCore::capture_grab_thumbnail(
    const std::string& target_json,
    int max_width,
    int max_height)
{
    auto result = media_->grab_thumbnail(target_json, max_width, max_height);
    return { result.width, result.height, std::move(result.rgba) };
}

bool DriscordCore::screen_start_sharing(const std::string& target_json,
    int max_width,
    int max_height,
    int fps,
    bool share_audio)
{
    return media_->start_sharing(
        target_json, max_width, max_height, fps, share_audio);
}

void DriscordCore::screen_stop_sharing()
{
    media_->stop_sharing();
}

bool DriscordCore::screen_sharing() const
{
    return media_->sharing();
}

void DriscordCore::screen_set_local_preview_enabled(bool enabled)
{
    media_->set_local_preview_enabled(enabled);
}

std::string DriscordCore::screen_stats_json(const std::string& peer) const
{
    return media_->screen_stats_json(peer);
}

void DriscordCore::screen_set_stream_volume(
    const std::string& peer, float volume)
{
    media_->set_stream_volume(peer, volume);
}

float DriscordCore::screen_stream_volume(const std::string& peer) const
{
    return media_->stream_volume(peer);
}
