#include "driscord_core.hpp"
#include "audio/capture/system_audio_capture.hpp"
#include "utils/vector_view.hpp"
#include "video/capture/screen_capture.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

DriscordCore::DriscordCore()
    : audio_transport(transport)
    , video_transport(transport) {
    transport.on_peer_joined([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_peer_joined_cb_) on_peer_joined_cb_(id);
    });
    transport.on_peer_left([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_peer_left_cb_) on_peer_left_cb_(id);
    });
    video_transport.on_new_streaming_peer([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_new_streaming_peer_cb_) on_new_streaming_peer_cb_(id);
    });
    video_transport.on_streaming_peer_removed([this](const std::string& id) {
        std::scoped_lock lk(cb_mtx_);
        if (on_streaming_peer_removed_cb_) on_streaming_peer_removed_cb_(id);
    });
}

// ---------------------------------------------------------------------------
// Callback setters
// ---------------------------------------------------------------------------

void DriscordCore::set_on_peer_joined(StringCb cb) {
    std::scoped_lock lk(cb_mtx_);
    on_peer_joined_cb_ = std::move(cb);
}

void DriscordCore::set_on_peer_left(StringCb cb) {
    std::scoped_lock lk(cb_mtx_);
    on_peer_left_cb_ = std::move(cb);
}

void DriscordCore::set_on_new_streaming_peer(StringCb cb) {
    std::scoped_lock lk(cb_mtx_);
    on_new_streaming_peer_cb_ = std::move(cb);
}

void DriscordCore::set_on_streaming_peer_removed(StringCb cb) {
    std::scoped_lock lk(cb_mtx_);
    on_streaming_peer_removed_cb_ = std::move(cb);
}

void DriscordCore::set_on_frame(FrameCb cb) {
    std::scoped_lock lk(cb_mtx_);
    on_frame_cb_ = std::move(cb);
}

void DriscordCore::set_on_frame_removed(StringCb cb) {
    std::scoped_lock lk(cb_mtx_);
    on_frame_removed_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// ScreenSession lifecycle
// ---------------------------------------------------------------------------

void DriscordCore::init_screen_session(int buf_ms, int max_sync_ms) {
    screen_session.emplace(
        buf_ms,
        std::chrono::milliseconds(max_sync_ms),
        [this](const uint8_t* d, size_t l) { video_transport.send_video(d, l); },
        [this]() { video_transport.send_keyframe_request(); },
        [this](const uint8_t* d, size_t l) { audio_transport.send_screen_audio(d, l); }
    );
    screen_session->set_on_frame(
        [this](const std::string& pid, const uint8_t* rgba, int w, int h) {
            std::scoped_lock lk(cb_mtx_);
            if (on_frame_cb_) on_frame_cb_(pid, rgba, w, h);
        }
    );
    screen_session->set_on_frame_removed([this](const std::string& pid) {
        on_video_peer_stream_ended(pid);
        std::scoped_lock lk(cb_mtx_);
        if (on_frame_removed_cb_) on_frame_removed_cb_(pid);
    });
    video_transport.set_video_sink(
        [this](const std::string& peer_id, const uint8_t* data, size_t len) {
            screen_session
                ->push_video_packet(peer_id, utils::vector_view<const uint8_t>{data, len});
        },
        [this]() {
            if (screen_session->sharing()) {
                screen_session->force_keyframe();
            }
        }
    );
}

void DriscordCore::deinit_screen_session() {
    video_transport.clear_video_sink();
    screen_session.reset();
}

// ---------------------------------------------------------------------------
// Stream watching
// ---------------------------------------------------------------------------

void DriscordCore::join_stream(const std::string& peer_id) {
    watched_peers_.insert(peer_id);
    screen_session->add_video_peer(peer_id);
    screen_session->add_audio_peer(peer_id);
    audio_transport.set_screen_audio_recv(peer_id, screen_session->audio_receiver(peer_id));
    audio_transport.add_screen_audio_to_mixer(peer_id);
    video_transport.add_watched_peer(peer_id);
    video_transport.send_keyframe_request();
}

void DriscordCore::leave_stream() {
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

void DriscordCore::on_video_peer_stream_ended(const std::string& peer_id) {
    video_transport.remove_streaming_peer(peer_id);
}

// ---------------------------------------------------------------------------
// Transport facade
// ---------------------------------------------------------------------------

void DriscordCore::add_turn_server(
    const std::string& url,
    const std::string& user,
    const std::string& pass
) {
    transport.add_turn_server(url, user, pass);
}

void DriscordCore::connect(const std::string& url) {
    transport.connect(url);
}

void DriscordCore::disconnect() {
    transport.disconnect();
}

bool DriscordCore::connected() const {
    return transport.connected();
}

std::string DriscordCore::local_id() const {
    return transport.local_id();
}

std::string DriscordCore::peers_json() const {
    auto ps  = transport.peers();
    json arr = json::array();
    for (auto& p : ps) {
        arr.push_back({{"id", p.id}, {"connected", p.primary_open}});
    }
    return arr.dump();
}

// ---------------------------------------------------------------------------
// Audio facade
// ---------------------------------------------------------------------------

void DriscordCore::audio_send(const uint8_t* data, int len) {
    audio_transport.send_audio(data, len);
}

bool DriscordCore::audio_start() {
    return audio_transport.start();
}

void DriscordCore::audio_stop() {
    audio_transport.stop();
}

bool DriscordCore::audio_deafened() const {
    return audio_transport.deafened();
}

void DriscordCore::audio_set_deafened(bool d) {
    audio_transport.set_deafened(d);
}

float DriscordCore::audio_master_volume() const {
    return audio_transport.master_volume();
}

void DriscordCore::audio_set_master_volume(float v) {
    audio_transport.set_master_volume(v);
}

float DriscordCore::audio_output_level() const {
    return audio_transport.output_level();
}

bool DriscordCore::audio_self_muted() const {
    return audio_transport.self_muted();
}

void DriscordCore::audio_set_self_muted(bool m) {
    audio_transport.set_self_muted(m);
}

float DriscordCore::audio_input_level() const {
    return audio_transport.input_level();
}

void DriscordCore::audio_on_peer_joined(const std::string& peer, int jitter_ms) {
    audio_transport.on_peer_joined(peer, jitter_ms);
}

void DriscordCore::audio_on_peer_left(const std::string& peer) {
    audio_transport.on_peer_left(peer);
}

void DriscordCore::audio_set_peer_volume(const std::string& peer, float vol) {
    audio_transport.set_peer_volume(peer, vol);
}

float DriscordCore::audio_peer_volume(const std::string& peer) const {
    return audio_transport.peer_volume(peer);
}

void DriscordCore::audio_set_peer_muted(const std::string& peer, bool m) {
    audio_transport.set_peer_muted(peer, m);
}

bool DriscordCore::audio_peer_muted(const std::string& peer) const {
    return audio_transport.peer_muted(peer);
}

void DriscordCore::audio_set_screen_audio_receiver(const std::string& peer, bool has_screen) {
    std::shared_ptr<AudioReceiver> recv;
    if (has_screen) {
        recv = screen_session->audio_receiver(peer);
    }
    audio_transport.set_screen_audio_recv(peer, std::move(recv));
}

void DriscordCore::audio_unset_screen_audio_receiver(const std::string& peer) {
    audio_transport.unset_screen_audio_recv(peer);
}

void DriscordCore::audio_add_screen_audio_to_mixer(const std::string& peer) {
    audio_transport.add_screen_audio_to_mixer(peer);
}

void DriscordCore::audio_remove_screen_audio_from_mixer(const std::string& peer) {
    audio_transport.remove_screen_audio_from_mixer(peer);
}

// ---------------------------------------------------------------------------
// Video facade
// ---------------------------------------------------------------------------

void DriscordCore::video_set_watching(bool w) {
    if (!w) {
        video_transport.clear_watched_peers();
    }
}

bool DriscordCore::video_watching() const {
    return video_transport.watching();
}

void DriscordCore::video_remove_streaming_peer(const std::string& peer) {
    video_transport.remove_streaming_peer(peer);
}

void DriscordCore::video_send_keyframe_request() {
    video_transport.send_keyframe_request();
}

// ---------------------------------------------------------------------------
// Capture facade
// ---------------------------------------------------------------------------

bool DriscordCore::capture_system_audio_available() const {
    return SystemAudioCapture::available();
}

std::string DriscordCore::capture_list_targets_json() const {
    auto targets = ScreenCapture::list_targets();
    json arr     = json::array();
    for (auto& t : targets) {
        arr.push_back(
            {{"type", t.type == CaptureTarget::Monitor ? 0 : 1},
             {"id", t.id},
             {"name", t.name},
             {"width", t.width},
             {"height", t.height},
             {"x", t.x},
             {"y", t.y}}
        );
    }
    return arr.dump(-1, ' ', /*ensure_ascii=*/true, nlohmann::json::error_handler_t::replace);
}

std::vector<uint8_t> DriscordCore::capture_grab_thumbnail(
    const std::string& target_json,
    int max_w,
    int max_h
) {
    auto target = CaptureTarget::from_json(json::parse(target_json));
    auto frame  = ScreenCapture::grab_thumbnail(target, max_w, max_h);
    if (frame.data.empty()) {
        return {};
    }
    return frame.to_rgba();
}

// ---------------------------------------------------------------------------
// Screen facade
// ---------------------------------------------------------------------------

bool DriscordCore::screen_start_sharing(
    const std::string& target_json,
    int max_w,
    int max_h,
    int fps,
    int bitrate_kbps,
    bool share_audio
) {
    auto target = CaptureTarget::from_json(json::parse(target_json));
    return screen_session->start_sharing(target, max_w, max_h, fps, bitrate_kbps, share_audio);
}

void DriscordCore::screen_stop_sharing() {
    screen_session->stop_sharing();
    video_transport.send_stop_stream();
}

bool DriscordCore::screen_sharing() const {
    return screen_session->sharing();
}

bool DriscordCore::screen_sharing_audio() const {
    return screen_session->sharing_audio();
}

void DriscordCore::screen_force_keyframe() {
    screen_session->force_keyframe();
}

void DriscordCore::screen_update() {
    screen_session->update();
}

std::string DriscordCore::screen_active_peer() const {
    return screen_session->active_peer();
}

bool DriscordCore::screen_active() const {
    return screen_session->active();
}

void DriscordCore::screen_reset() {
    screen_session->reset();
}

void DriscordCore::screen_reset_audio() {
    screen_session->reset_audio();
}

void DriscordCore::screen_set_stream_volume(const std::string& peer, float vol) {
    audio_transport.set_screen_audio_peer_volume(peer, vol);
}

float DriscordCore::screen_stream_volume() const {
    return audio_transport.screen_audio_peer_volume(screen_session->active_peer());
}

std::string DriscordCore::screen_stats_json() const {
    return screen_session->stats_json();
}
