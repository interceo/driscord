#include "webrtc/google_webrtc_client.hpp"

#include "audio/capture/system_audio_capture.hpp"
#include "config.hpp"
#include "log.hpp"
#include "transport.hpp"
#include "utils/signaling_protocol.hpp"
#include "webrtc/google_webrtc_pcm_playout.hpp"
#include "webrtc/google_webrtc_runtime.hpp"
#include "webrtc/google_webrtc_screen_session.hpp"
#include "webrtc/google_webrtc_screen_stats.hpp"
#include "webrtc/google_webrtc_voice_session.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace {

driscord::media::DesktopCaptureKind capture_kind(int type)
{
    return type == 0 ? driscord::media::DesktopCaptureKind::Screen
                     : driscord::media::DesktopCaptureKind::Window;
}

struct CaptureSelection {
    driscord::media::DesktopCaptureKind kind;
    int64_t id;
};

std::optional<CaptureSelection> parse_capture_selection(
    const std::string& target_json)
{
    try {
        const auto value = json::parse(target_json);
        if (!value.contains("id") || !value["id"].is_string()) {
            return std::nullopt;
        }
        size_t consumed = 0;
        const std::string id_text = value["id"].get<std::string>();
        const int64_t id = std::stoll(id_text, &consumed);
        if (consumed != id_text.size()) {
            return std::nullopt;
        }
        return CaptureSelection {
            .kind = capture_kind(value.value("type", 0)),
            .id = id,
        };
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

float clamp_volume(float volume)
{
    return std::clamp(volume, 0.0f, 2.0f);
}

} // namespace

struct GoogleWebRtcClient::Impl
    : std::enable_shared_from_this<GoogleWebRtcClient::Impl> {
    explicit Impl(Transport& transport_value, Callbacks callbacks_value)
        : transport(transport_value)
        , callbacks(std::move(callbacks_value))
        , screen_runtime(driscord::media::GoogleWebRtcRuntimeConfig {
              .injected_audio_device = driscord::media::InjectedAudioDeviceConfig {
                  .sample_rate_hz = GoogleWebRtcPcmPlayout::kSampleRate,
                  .channels = GoogleWebRtcPcmPlayout::kChannels,
                  .max_buffered_frames = 100,
                  .on_rendered_audio =
                      [this](std::span<const int16_t> samples, int rate,
                          size_t channels) {
                          if (rate == GoogleWebRtcPcmPlayout::kSampleRate
                              && channels
                                  == GoogleWebRtcPcmPlayout::kChannels) {
                              screen_playout.push(samples);
                          }
                      },
              },
          })
    {
    }

    ~Impl()
    {
        stop_screen_session();
        stop_voice_session();
        screen_playout.stop();
    }

    void bind_callbacks()
    {
        const std::weak_ptr<Impl> weak = weak_from_this();
        transport.add_connection_listener([weak](bool connected) {
            if (auto self = weak.lock()) {
                self->on_signaling_state(connected);
            }
        });
        transport.add_media_answer_listener(
            [weak](signaling::ConnectionId connection, const std::string& sdp) {
                if (auto self = weak.lock()) {
                    self->apply_answer(connection, sdp);
                }
            });
        transport.add_media_candidate_listener(
            [weak](signaling::ConnectionId connection,
                const std::string& candidate,
                const std::string& mid) {
                if (auto self = weak.lock()) {
                    self->apply_candidate(connection, candidate, mid);
                }
            });
        transport.add_media_track_binding_listener(
            [weak](signaling::ConnectionId connection,
                const std::string& mid,
                const std::optional<driscord::PeerId>& peer_id) {
                if (auto self = weak.lock()) {
                    self->apply_binding(connection, mid, peer_id);
                }
            });
    }

    void on_signaling_state(bool connected)
    {
        if (connected) {
            start_voice_if_requested();
            start_screen_if_requested();
        } else {
            stop_voice_session();
            stop_screen_session();
        }
    }

    void apply_answer(signaling::ConnectionId connection,
        const std::string& sdp)
    {
        if (connection == signaling::ConnectionId::Voice) {
            std::scoped_lock lifecycle_lock(voice_lifecycle_mutex);
            driscord::media::GoogleWebRtcVoiceSession* session = nullptr;
            {
                std::scoped_lock lock(mutex);
                session = voice_session.get();
            }
            if (session) {
                session->apply_answer(sdp);
            }
        } else if (connection == signaling::ConnectionId::Screen) {
            std::scoped_lock lifecycle_lock(screen_lifecycle_mutex);
            std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> session;
            {
                std::scoped_lock lock(mutex);
                session = screen_session;
            }
            if (session) {
                session->apply_answer(sdp);
            }
        }
    }

    void apply_candidate(signaling::ConnectionId connection,
        const std::string& candidate,
        const std::string& mid)
    {
        if (connection == signaling::ConnectionId::Voice) {
            std::scoped_lock lifecycle_lock(voice_lifecycle_mutex);
            driscord::media::GoogleWebRtcVoiceSession* session = nullptr;
            {
                std::scoped_lock lock(mutex);
                session = voice_session.get();
            }
            if (session) {
                session->add_remote_candidate(candidate, mid);
            }
        } else if (connection == signaling::ConnectionId::Screen) {
            std::scoped_lock lifecycle_lock(screen_lifecycle_mutex);
            std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> session;
            {
                std::scoped_lock lock(mutex);
                session = screen_session;
            }
            if (session) {
                session->add_remote_candidate(candidate, mid);
            }
        }
    }

    void apply_binding(signaling::ConnectionId connection,
        const std::string& mid,
        const std::optional<driscord::PeerId>& peer_id)
    {
        PeerCallback removed;
        std::string removed_peer;
        {
            std::scoped_lock lock(mutex);
            auto& bindings = connection == signaling::ConnectionId::Voice
                ? voice_bindings
                : screen_bindings;
            const auto old = bindings.find(mid);
            const bool binding_changed = old == bindings.end()
                ? peer_id.has_value()
                : !peer_id || old->second != peer_id->value;
            if (connection == signaling::ConnectionId::Screen
                && old != bindings.end() && binding_changed
                && screen_video_mids.contains(mid)) {
                removed_peer = old->second;
                removed = callbacks.on_frame_removed;
            }
            if (connection == signaling::ConnectionId::Screen
                && binding_changed) {
                screen_stats.set_binding(mid,
                    peer_id ? std::optional<std::string>(peer_id->value)
                            : std::nullopt);
            }
            if (peer_id) {
                bindings[mid] = peer_id->value;
            } else {
                bindings.erase(mid);
            }
            if (connection == signaling::ConnectionId::Voice) {
                apply_voice_mid_locked(mid);
            } else if (connection == signaling::ConnectionId::Screen) {
                apply_screen_audio_mid_locked(mid);
            }
        }
        if (removed && !removed_peer.empty()) {
            removed(removed_peer);
        }
    }

    void start_voice_if_requested()
    {
        std::scoped_lock lifecycle_lock(voice_lifecycle_mutex);
        std::scoped_lock lock(mutex);
        if (!voice_requested || voice_session || !transport.connected()) {
            return;
        }
        const std::weak_ptr<Impl> weak = weak_from_this();
        driscord::media::VoiceSessionCallbacks session_callbacks;
        session_callbacks.on_offer = [this](std::string sdp) {
            transport.send_media_offer(signaling::ConnectionId::Voice, sdp);
        };
        session_callbacks.on_candidate =
            [this](std::string candidate, std::string mid) {
                transport.send_media_candidate(signaling::ConnectionId::Voice,
                    candidate, mid);
            };
        session_callbacks.on_remote_track =
            [weak](std::string mid, std::string) {
                if (auto self = weak.lock()) {
                    std::scoped_lock callback_lock(self->mutex);
                    self->apply_voice_mid_locked(mid);
                }
            };
        session_callbacks.on_error = [](std::string message) {
            LOG_ERROR() << "Google WebRTC voice: " << message;
        };
        auto next = std::make_unique<driscord::media::GoogleWebRtcVoiceSession>(
            voice_runtime,
            driscord::media::VoiceSessionConfig {
                .remote_track_slots = 16,
                .microphone_enabled = !voice_muted,
            },
            std::move(session_callbacks));
        if (!next->start()) {
            LOG_ERROR() << "Google WebRTC voice session failed to start";
            return;
        }
        voice_session = std::move(next);
    }

    void start_screen_if_requested()
    {
        std::scoped_lock lifecycle_lock(screen_lifecycle_mutex);
        std::scoped_lock lock(mutex);
        if (!screen_requested || screen_session || !transport.connected()) {
            return;
        }
        if (!screen_playout.start()) {
            LOG_WARNING()
                << "Google WebRTC screen playout device is unavailable; "
                   "screen video remains enabled";
        }
        const std::weak_ptr<Impl> weak = weak_from_this();
        driscord::media::ScreenSessionCallbacks session_callbacks;
        session_callbacks.on_offer = [this](std::string sdp) {
            transport.send_media_offer(signaling::ConnectionId::Screen, sdp);
        };
        session_callbacks.on_candidate =
            [this](std::string candidate, std::string mid) {
                transport.send_media_candidate(signaling::ConnectionId::Screen,
                    candidate, mid);
            };
        session_callbacks.on_remote_track =
            [weak](std::string mid, std::string, bool video) {
                if (auto self = weak.lock()) {
                    std::scoped_lock callback_lock(self->mutex);
                    if (video) {
                        self->screen_video_mids.insert(mid);
                    } else {
                        self->screen_audio_mids.insert(mid);
                        self->apply_screen_audio_mid_locked(mid);
                    }
                }
            };
        session_callbacks.on_remote_video =
            [weak](std::string_view mid,
                driscord::media::DecodedVideoFrameView frame) {
                if (auto self = weak.lock()) {
                    self->deliver_video(mid, frame);
                }
            };
        session_callbacks.on_error = [](std::string message) {
            LOG_ERROR() << "Google WebRTC screen: " << message;
        };
        auto next = std::make_shared<driscord::media::GoogleWebRtcScreenSession>(
            screen_runtime,
            driscord::media::ScreenSessionConfig {
                .remote_stream_slots = 8,
                .sharing_enabled = false,
                .system_audio_enabled = false,
                .max_video_bitrate_bps = 4'000'000,
                .max_system_audio_bitrate_bps = stream_defaults::kSystemAudioBitrateKbps * 1000,
            },
            std::move(session_callbacks));
        if (!next->start()) {
            LOG_ERROR() << "Google WebRTC screen session failed to start";
            screen_playout.stop();
            return;
        }
        screen_session = std::move(next);
        // A reconnect creates a fresh server-side room/session, so targeted
        // subscriptions must be replayed even though the local watched set is
        // intentionally preserved across signaling loss.
        for (const auto& peer : watched_peers) {
            transport.send_watch_start(peer);
        }
    }

    void stop_voice_session()
    {
        std::scoped_lock lifecycle_lock(voice_lifecycle_mutex);
        std::unique_ptr<driscord::media::GoogleWebRtcVoiceSession> old;
        {
            std::scoped_lock lock(mutex);
            old = std::move(voice_session);
            voice_bindings.clear();
        }
        if (old) {
            old->close();
        }
    }

    void stop_screen_session()
    {
        std::scoped_lock lifecycle_lock(screen_lifecycle_mutex);
        std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> old;
        std::unique_ptr<SystemAudioCapture> old_capture;
        std::vector<std::string> removed_peers;
        {
            std::scoped_lock lock(mutex);
            old = std::move(screen_session);
            old_capture = std::move(system_audio_capture);
            for (const auto& [mid, peer] : screen_bindings) {
                if (screen_video_mids.contains(mid)) {
                    removed_peers.push_back(peer);
                }
            }
            screen_bindings.clear();
            screen_video_mids.clear();
            screen_audio_mids.clear();
            screen_stats.reset_session();
            sharing_active = false;
        }
        if (old_capture) {
            old_capture->stop();
        }
        system_audio_position = 0;
        if (old) {
            old->stop_desktop_capture();
            old->close();
        }
        screen_playout.stop();
        std::sort(removed_peers.begin(), removed_peers.end());
        removed_peers.erase(
            std::unique(removed_peers.begin(), removed_peers.end()),
            removed_peers.end());
        if (callbacks.on_frame_removed) {
            for (const auto& peer : removed_peers) {
                callbacks.on_frame_removed(peer);
            }
        }
    }

    void deliver_video(std::string_view mid,
        driscord::media::DecodedVideoFrameView frame)
    {
        FrameCallback on_frame;
        std::string peer;
        {
            std::scoped_lock lock(mutex);
            const auto binding = screen_bindings.find(std::string(mid));
            if (binding == screen_bindings.end()
                || !watched_peers.contains(binding->second)) {
                return;
            }
            peer = binding->second;
            on_frame = callbacks.on_frame;
        }
        if (on_frame) {
            on_frame(peer, frame.rgba.data(), frame.width, frame.height);
        }
    }

    void apply_voice_mid_locked(const std::string& mid)
    {
        if (!voice_session) {
            return;
        }
        const auto binding = voice_bindings.find(mid);
        const bool bound = binding != voice_bindings.end();
        bool enabled = bound && !voice_deafened;
        float volume = master_volume_value;
        if (bound) {
            enabled = enabled && !voice_peer_muted[binding->second];
            const auto it = voice_peer_volumes.find(binding->second);
            volume *= it == voice_peer_volumes.end() ? 1.0f : it->second;
        }
        voice_session->set_remote_track_enabled(mid, enabled);
        if (bound) {
            voice_session->set_remote_track_volume(mid, volume);
        }
    }

    void apply_screen_audio_mid_locked(const std::string& mid)
    {
        if (!screen_session || !screen_audio_mids.contains(mid)) {
            return;
        }
        const auto binding = screen_bindings.find(mid);
        const bool enabled = binding != screen_bindings.end()
            && watched_peers.contains(binding->second);
        screen_session->set_remote_audio_enabled(mid, enabled);
        if (binding != screen_bindings.end()) {
            const auto it = screen_peer_volumes.find(binding->second);
            screen_session->set_remote_audio_volume(mid,
                it == screen_peer_volumes.end() ? 1.0f : it->second);
        }
    }

    void submit_system_audio(
        const float* samples, size_t frames, int channels)
    {
        if (!samples || channels != static_cast<int>(GoogleWebRtcPcmPlayout::kChannels)) {
            return;
        }
        constexpr size_t kSamplesPerFrame = GoogleWebRtcPcmPlayout::kSampleRate / 100
            * GoogleWebRtcPcmPlayout::kChannels;
        for (size_t i = 0; i < frames * static_cast<size_t>(channels); ++i) {
            const float clamped = std::clamp(samples[i], -1.0f, 1.0f);
            system_audio_frame[system_audio_position++] = static_cast<int16_t>(std::lrint(clamped * 32767.0f));
            if (system_audio_position == kSamplesPerFrame) {
                (void)screen_runtime.submit_recorded_audio_10ms(
                    system_audio_frame);
                system_audio_position = 0;
            }
        }
    }

    Transport& transport;
    const Callbacks callbacks;
    GoogleWebRtcPcmPlayout screen_playout;
    driscord::media::GoogleWebRtcRuntime voice_runtime;
    driscord::media::GoogleWebRtcRuntime screen_runtime;
    std::mutex voice_lifecycle_mutex;
    std::mutex screen_lifecycle_mutex;
    mutable std::mutex mutex;
    std::unique_ptr<driscord::media::GoogleWebRtcVoiceSession> voice_session;
    std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> screen_session;
    std::unique_ptr<SystemAudioCapture> system_audio_capture;
    std::unordered_map<std::string, std::string> voice_bindings;
    std::unordered_map<std::string, std::string> screen_bindings;
    std::unordered_set<std::string> screen_video_mids;
    std::unordered_set<std::string> screen_audio_mids;
    std::unordered_set<std::string> watched_peers;
    std::unordered_map<std::string, float> voice_peer_volumes;
    std::unordered_map<std::string, bool> voice_peer_muted;
    std::unordered_map<std::string, float> screen_peer_volumes;
    std::array<int16_t, GoogleWebRtcPcmPlayout::kSampleRate / 100 * GoogleWebRtcPcmPlayout::kChannels>
        system_audio_frame { };
    size_t system_audio_position = 0;
    float master_volume_value = 1.0f;
    bool voice_requested = false;
    bool voice_muted = false;
    bool voice_deafened = false;
    bool screen_requested = false;
    bool sharing_active = false;
    driscord::media::ScreenStatsTracker screen_stats;
};

GoogleWebRtcClient::GoogleWebRtcClient(
    Transport& transport, Callbacks callbacks)
    : impl_(std::make_shared<Impl>(transport, std::move(callbacks)))
{
    impl_->bind_callbacks();
}

GoogleWebRtcClient::~GoogleWebRtcClient() = default;

void GoogleWebRtcClient::start_voice()
{
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->voice_requested = true;
    }
    impl_->start_voice_if_requested();
}

void GoogleWebRtcClient::stop_voice()
{
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->voice_requested = false;
    }
    impl_->stop_voice_session();
}

void GoogleWebRtcClient::set_muted(bool muted)
{
    std::scoped_lock lock(impl_->mutex);
    impl_->voice_muted = muted;
    if (impl_->voice_session) {
        impl_->voice_session->set_microphone_enabled(!muted);
    }
}

bool GoogleWebRtcClient::muted() const
{
    std::scoped_lock lock(impl_->mutex);
    return impl_->voice_muted;
}

void GoogleWebRtcClient::set_deafened(bool deafened)
{
    std::scoped_lock lock(impl_->mutex);
    impl_->voice_deafened = deafened;
    for (const auto& [mid, _] : impl_->voice_bindings) {
        impl_->apply_voice_mid_locked(mid);
    }
}

bool GoogleWebRtcClient::deafened() const
{
    std::scoped_lock lock(impl_->mutex);
    return impl_->voice_deafened;
}

void GoogleWebRtcClient::set_master_volume(float volume)
{
    std::scoped_lock lock(impl_->mutex);
    impl_->master_volume_value = clamp_volume(volume);
    for (const auto& [mid, _] : impl_->voice_bindings) {
        impl_->apply_voice_mid_locked(mid);
    }
}

float GoogleWebRtcClient::master_volume() const
{
    std::scoped_lock lock(impl_->mutex);
    return impl_->master_volume_value;
}

void GoogleWebRtcClient::set_peer_volume(
    const std::string& peer_id, float volume)
{
    std::scoped_lock lock(impl_->mutex);
    impl_->voice_peer_volumes[peer_id] = clamp_volume(volume);
    for (const auto& [mid, peer] : impl_->voice_bindings) {
        if (peer == peer_id) {
            impl_->apply_voice_mid_locked(mid);
        }
    }
}

float GoogleWebRtcClient::peer_volume(const std::string& peer_id) const
{
    std::scoped_lock lock(impl_->mutex);
    const auto it = impl_->voice_peer_volumes.find(peer_id);
    return it == impl_->voice_peer_volumes.end() ? 1.0f : it->second;
}

void GoogleWebRtcClient::set_peer_muted(
    const std::string& peer_id, bool muted)
{
    std::scoped_lock lock(impl_->mutex);
    impl_->voice_peer_muted[peer_id] = muted;
    for (const auto& [mid, peer] : impl_->voice_bindings) {
        if (peer == peer_id) {
            impl_->apply_voice_mid_locked(mid);
        }
    }
}

bool GoogleWebRtcClient::peer_muted(const std::string& peer_id) const
{
    std::scoped_lock lock(impl_->mutex);
    const auto it = impl_->voice_peer_muted.find(peer_id);
    return it != impl_->voice_peer_muted.end() && it->second;
}

void GoogleWebRtcClient::init_screen()
{
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->screen_requested = true;
    }
    impl_->start_screen_if_requested();
}

void GoogleWebRtcClient::deinit_screen()
{
    std::vector<std::string> watched;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->screen_requested = false;
        watched.assign(
            impl_->watched_peers.begin(), impl_->watched_peers.end());
        impl_->watched_peers.clear();
        impl_->screen_stats.clear_watched();
    }
    for (const auto& peer : watched) {
        impl_->transport.send_watch_stop(peer);
    }
    impl_->stop_screen_session();
}

void GoogleWebRtcClient::join_stream(const std::string& peer_id)
{
    bool inserted = false;
    {
        std::scoped_lock lock(impl_->mutex);
        inserted = impl_->watched_peers.insert(peer_id).second;
        if (inserted) {
            impl_->screen_stats.set_watched(peer_id, true);
        }
        for (const auto& mid : impl_->screen_audio_mids) {
            impl_->apply_screen_audio_mid_locked(mid);
        }
    }
    if (inserted) {
        impl_->transport.send_watch_start(peer_id);
    }
}

void GoogleWebRtcClient::leave_streams()
{
    std::vector<std::string> removed;
    {
        std::scoped_lock lock(impl_->mutex);
        removed.assign(
            impl_->watched_peers.begin(), impl_->watched_peers.end());
        impl_->watched_peers.clear();
        impl_->screen_stats.clear_watched();
        for (const auto& mid : impl_->screen_audio_mids) {
            impl_->apply_screen_audio_mid_locked(mid);
        }
    }
    for (const auto& peer : removed) {
        impl_->transport.send_watch_stop(peer);
    }
    if (impl_->callbacks.on_frame_removed) {
        for (const auto& peer : removed) {
            impl_->callbacks.on_frame_removed(peer);
        }
    }
}

void GoogleWebRtcClient::leave_stream(const std::string& peer_id)
{
    bool removed = false;
    {
        std::scoped_lock lock(impl_->mutex);
        removed = impl_->watched_peers.erase(peer_id) > 0;
        impl_->screen_stats.set_watched(peer_id, false);
        for (const auto& mid : impl_->screen_audio_mids) {
            impl_->apply_screen_audio_mid_locked(mid);
        }
    }
    if (!removed) {
        return;
    }
    impl_->transport.send_watch_stop(peer_id);
    if (impl_->callbacks.on_frame_removed) {
        impl_->callbacks.on_frame_removed(peer_id);
    }
}

void GoogleWebRtcClient::remove_peer(const std::string& peer_id)
{
    bool removed = false;
    {
        std::scoped_lock lock(impl_->mutex);
        removed = impl_->watched_peers.erase(peer_id) > 0;
        impl_->voice_peer_volumes.erase(peer_id);
        impl_->voice_peer_muted.erase(peer_id);
        impl_->screen_peer_volumes.erase(peer_id);
        impl_->screen_stats.remove_peer(peer_id);
        for (const auto& mid : impl_->screen_audio_mids) {
            impl_->apply_screen_audio_mid_locked(mid);
        }
    }
    if (removed) {
        impl_->transport.send_watch_stop(peer_id);
    }
    if (removed && impl_->callbacks.on_frame_removed) {
        impl_->callbacks.on_frame_removed(peer_id);
    }
}

bool GoogleWebRtcClient::watching() const
{
    std::scoped_lock lock(impl_->mutex);
    return !impl_->watched_peers.empty();
}

std::string GoogleWebRtcClient::video_targets_json() const
{
    json result = json::array();
    for (const auto kind : { driscord::media::DesktopCaptureKind::Screen,
             driscord::media::DesktopCaptureKind::Window }) {
        for (const auto& source :
            driscord::media::GoogleWebRtcScreenSession::list_desktop_sources(
                kind)) {
            result.push_back({
                { "type",
                    kind == driscord::media::DesktopCaptureKind::Screen ? 0
                                                                        : 1 },
                { "id", std::to_string(source.id) },
                { "name", source.title },
                { "width", 0 },
                { "height", 0 },
                { "x", 0 },
                { "y", 0 },
            });
        }
    }
    return result.dump(-1, ' ', false,
        nlohmann::json::error_handler_t::replace);
}

GoogleWebRtcClient::Thumbnail GoogleWebRtcClient::grab_thumbnail(
    const std::string& target_json, int max_width, int max_height) const
{
    const auto selection = parse_capture_selection(target_json);
    if (!selection) {
        return { };
    }
    auto thumbnail = driscord::media::GoogleWebRtcScreenSession::grab_desktop_thumbnail(
        selection->kind, selection->id, max_width, max_height);
    return {
        .width = thumbnail.width,
        .height = thumbnail.height,
        .rgba = std::move(thumbnail.rgba),
    };
}

bool GoogleWebRtcClient::start_sharing(const std::string& target_json,
    int max_width,
    int max_height,
    int fps,
    bool share_audio)
{
    const auto selection = parse_capture_selection(target_json);
    if (!selection) {
        return false;
    }

    std::scoped_lock lock(impl_->mutex);
    if (impl_->sharing_active || !impl_->screen_session
        || !impl_->screen_session->start_desktop_capture(selection->kind,
            selection->id, fps, max_width, max_height)) {
        return false;
    }
    if (share_audio) {
        auto capture = SystemAudioCapture::create();
        const std::weak_ptr<Impl> weak = impl_;
        if (!capture || !capture->start([weak](const float* samples, size_t frames, int channels) {
                if (auto self = weak.lock()) {
                    self->submit_system_audio(samples, frames, channels);
                }
            })) {
            impl_->screen_session->stop_desktop_capture();
            impl_->screen_session->set_sharing_enabled(false);
            return false;
        }
        impl_->system_audio_capture = std::move(capture);
    }
    impl_->screen_session->set_system_audio_enabled(share_audio);
    impl_->sharing_active = true;
    impl_->transport.send_streaming_start();
    return true;
}

void GoogleWebRtcClient::stop_sharing()
{
    std::unique_ptr<SystemAudioCapture> capture;
    {
        std::scoped_lock lock(impl_->mutex);
        capture = std::move(impl_->system_audio_capture);
        if (impl_->screen_session) {
            impl_->screen_session->set_system_audio_enabled(false);
            impl_->screen_session->set_sharing_enabled(false);
            impl_->screen_session->stop_desktop_capture();
        }
        impl_->sharing_active = false;
    }
    if (capture) {
        capture->stop();
    }
    impl_->system_audio_position = 0;
    impl_->transport.send_streaming_stop();
}

bool GoogleWebRtcClient::sharing() const
{
    std::scoped_lock lock(impl_->mutex);
    return impl_->sharing_active;
}

void GoogleWebRtcClient::set_stream_volume(
    const std::string& peer_id, float volume)
{
    std::scoped_lock lock(impl_->mutex);
    impl_->screen_peer_volumes[peer_id] = clamp_volume(volume);
    for (const auto& mid : impl_->screen_audio_mids) {
        const auto binding = impl_->screen_bindings.find(mid);
        if (binding != impl_->screen_bindings.end()
            && binding->second == peer_id) {
            impl_->apply_screen_audio_mid_locked(mid);
        }
    }
}

float GoogleWebRtcClient::stream_volume(const std::string& peer_id) const
{
    std::scoped_lock lock(impl_->mutex);
    const auto it = impl_->screen_peer_volumes.find(peer_id);
    return it == impl_->screen_peer_volumes.end() ? 1.0f : it->second;
}

std::string GoogleWebRtcClient::screen_stats_json(
    const std::string& peer_id) const
{
    std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> session;
    driscord::media::ScreenStatsTracker::Poll poll;
    {
        std::scoped_lock lock(impl_->mutex);
        session = impl_->screen_session;
        poll = impl_->screen_stats.poll(peer_id, static_cast<bool>(session));
    }
    if (poll.start_request) {
        const std::weak_ptr<Impl> weak = impl_;
        if (!session->get_stats(
                [weak, generation = poll.session_generation](
                    driscord::media::ScreenSessionStats stats) {
                    if (auto self = weak.lock()) {
                        std::scoped_lock lock(self->mutex);
                        self->screen_stats.consume(
                            std::move(stats), generation);
                    }
                })) {
            std::scoped_lock lock(impl_->mutex);
            impl_->screen_stats.request_failed(poll.session_generation);
        }
    }
    return poll.json;
}
