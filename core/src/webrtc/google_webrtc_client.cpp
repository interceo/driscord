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
#include <atomic>
#include <cmath>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
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

// Everything needed to start the same capture again after the screen session
// is recreated.
struct SharingRequest {
    CaptureSelection selection;
    int max_width = 0;
    int max_height = 0;
    int fps = 0;
    bool share_audio = false;
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
    struct VoiceTrackUpdate {
        std::shared_ptr<driscord::media::GoogleWebRtcVoiceSession> session;
        std::string mid;
        bool enabled = false;
        std::optional<double> volume;
    };

    struct ScreenAudioUpdate {
        std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> session;
        std::string mid;
        bool enabled = false;
        std::optional<double> volume;
    };

    Impl(Transport& transport_value, Callbacks callbacks_value,
        std::vector<driscord::media::IceServerConfig> ice_servers)
        : transport(transport_value)
        , callbacks(std::move(callbacks_value))
        , screen_playout(std::make_shared<GoogleWebRtcPcmPlayout>())
        , voice_runtime(driscord::media::GoogleWebRtcRuntimeConfig {
              // Voice uses the real audio device; only screen injects PCM.
              .injected_audio_device = std::nullopt,
              .ice_servers = ice_servers,
          })
        , screen_runtime(driscord::media::GoogleWebRtcRuntimeConfig {
              .injected_audio_device = driscord::media::InjectedAudioDeviceConfig {
                  .sample_rate_hz = GoogleWebRtcPcmPlayout::kSampleRate,
                  .channels = GoogleWebRtcPcmPlayout::kChannels,
                  .max_buffered_frames = 100,
                  .on_rendered_audio =
                      [playout = std::weak_ptr<GoogleWebRtcPcmPlayout>(
                           screen_playout)](
                          std::span<const int16_t> samples, int rate,
                          size_t channels) {
                          if (rate == GoogleWebRtcPcmPlayout::kSampleRate
                              && channels
                                  == GoogleWebRtcPcmPlayout::kChannels) {
                              if (auto sink = playout.lock()) {
                                  sink->push(samples);
                              }
                          }
                      },
              },
              .ice_servers = std::move(ice_servers),
          })
    {
    }

    ~Impl()
    {
        stop_screen_session();
        stop_voice_session();
        screen_playout->stop();
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
        transport.add_watch_rejected_listener(
            [weak](const std::string& peer_id,
                signaling::WatchRejectReason) {
                if (auto self = weak.lock()) {
                    self->handle_watch_rejected(peer_id);
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

    void recover_voice_session(const std::shared_ptr<int>& token)
    {
        {
            std::scoped_lock lock(mutex);
            if (!voice_requested || voice_session_token != token
                || !transport.connected()) {
                return;
            }
        }
        LOG_WARNING() << "Google WebRTC voice connection failed; "
                         "recreating PeerConnection";
        stop_voice_session();
        start_voice_if_requested();
    }

    void recover_screen_session(const std::shared_ptr<int>& token)
    {
        bool was_sharing = false;
        std::optional<SharingRequest> request;
        {
            std::scoped_lock lock(mutex);
            if (!screen_requested || screen_session_token != token
                || !transport.connected()) {
                return;
            }
            was_sharing = sharing_active;
            request = sharing_request;
        }
        LOG_WARNING() << "Google WebRTC screen connection failed; "
                         "recreating PeerConnection";
        // Viewers re-subscribe on the stop/start pair, so the announcement
        // still brackets the rebuilt session rather than spanning it.
        if (was_sharing) {
            transport.send_streaming_stop();
        }
        stop_screen_session();
        start_screen_if_requested();
        if (was_sharing && request && !apply_sharing(*request)) {
            LOG_ERROR() << "Google WebRTC screen capture could not be resumed "
                           "after recovery; the stream stays stopped";
            std::scoped_lock lock(mutex);
            sharing_request.reset();
        }
    }

    static void schedule_recovery(std::function<void()> task) noexcept
    {
        try {
            std::thread(std::move(task)).detach();
        } catch (const std::exception& error) {
            LOG_ERROR() << "Unable to schedule WebRTC recovery: "
                        << error.what();
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
        std::optional<VoiceTrackUpdate> voice_update;
        std::optional<ScreenAudioUpdate> screen_update;
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
                voice_update = voice_update_locked(mid);
            } else if (connection == signaling::ConnectionId::Screen) {
                screen_update = screen_update_locked(mid);
            }
        }
        apply(std::move(voice_update));
        apply(std::move(screen_update));
        if (removed && !removed_peer.empty()) {
            removed(removed_peer);
        }
    }

    void handle_watch_rejected(const std::string& peer_id)
    {
        bool removed = false;
        std::vector<ScreenAudioUpdate> updates;
        {
            std::scoped_lock lock(mutex);
            removed = watched_peers.erase(peer_id) > 0;
            screen_stats.set_watched(peer_id, false);
            updates = all_screen_updates_locked();
        }
        apply(std::move(updates));
        if (removed && callbacks.on_frame_removed) {
            callbacks.on_frame_removed(peer_id);
        }
    }

    void start_voice_if_requested()
    {
        std::scoped_lock lifecycle_lock(voice_lifecycle_mutex);
        bool microphone_enabled = false;
        {
            std::scoped_lock lock(mutex);
            if (!voice_requested || voice_session || !transport.connected()) {
                return;
            }
            microphone_enabled = !voice_muted;
        }
        const std::weak_ptr<Impl> weak = weak_from_this();
        const auto session_token = std::make_shared<int>(0);
        const auto recovery_scheduled = std::make_shared<std::atomic_bool>(false);
        driscord::media::VoiceSessionCallbacks session_callbacks;
        session_callbacks.on_offer = [weak](std::string sdp) {
            if (auto self = weak.lock()) {
                self->transport.send_media_offer(
                    signaling::ConnectionId::Voice, sdp);
            }
        };
        session_callbacks.on_candidate =
            [weak](std::string candidate, std::string mid) {
                if (auto self = weak.lock()) {
                    self->transport.send_media_candidate(
                        signaling::ConnectionId::Voice, candidate, mid);
                }
            };
        session_callbacks.on_remote_track =
            [weak](std::string mid, std::string) {
                if (auto self = weak.lock()) {
                    std::optional<VoiceTrackUpdate> update;
                    {
                        std::scoped_lock callback_lock(self->mutex);
                        update = self->voice_update_locked(mid);
                    }
                    apply(std::move(update));
                }
            };
        session_callbacks.on_state =
            [weak, session_token, recovery_scheduled](
                driscord::media::VoiceConnectionState state) {
                if (state != driscord::media::MediaConnectionState::Failed
                    || recovery_scheduled->exchange(true)) {
                    return;
                }
                schedule_recovery([weak, session_token] {
                    if (auto self = weak.lock()) {
                        self->recover_voice_session(session_token);
                    }
                });
            };
        session_callbacks.on_error = [](std::string message) {
            LOG_ERROR() << "Google WebRTC voice: " << message;
        };
        auto next = std::make_shared<driscord::media::GoogleWebRtcVoiceSession>(
            voice_runtime,
            driscord::media::VoiceSessionConfig {
                .remote_track_slots = stream_defaults::kVoiceReceiveSlots,
                .microphone_enabled = microphone_enabled,
                .max_microphone_bitrate_bps
                = stream_defaults::kVoiceBitrateKbps * 1000,
            },
            std::move(session_callbacks));
        if (!next->start()) {
            LOG_ERROR() << "Google WebRTC voice session failed to start";
            return;
        }
        apply_selected_audio_devices();
        std::vector<VoiceTrackUpdate> updates;
        bool retained = false;
        {
            std::scoped_lock lock(mutex);
            if (voice_requested && !voice_session && transport.connected()) {
                voice_session = next;
                voice_session_token = session_token;
                updates = all_voice_updates_locked();
                retained = true;
            }
        }
        if (!retained) {
            next->close();
            return;
        }
        apply(std::move(updates));
    }

    // Starts the capture described by `request` against whatever screen session
    // exists now. Shared by the user-initiated path and by recovery, so a
    // recreated PeerConnection resumes the same stream.
    [[nodiscard]] bool apply_sharing(const SharingRequest& request)
    {
        std::scoped_lock sharing_lock(sharing_mutex);
        std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> session;
        {
            std::scoped_lock lock(mutex);
            if (sharing_active || !screen_requested || !screen_session) {
                return false;
            }
            session = screen_session;
        }
        if (!session->start_desktop_capture(request.selection.kind,
                request.selection.id, request.fps, request.max_width,
                request.max_height)) {
            return false;
        }

        std::unique_ptr<SystemAudioCapture> capture;
        if (request.share_audio) {
            capture = SystemAudioCapture::create();
            const std::weak_ptr<Impl> weak = weak_from_this();
            if (!capture
                || !capture->start([weak](const float* samples, size_t frames,
                                       int channels) {
                       if (auto self = weak.lock()) {
                           self->submit_system_audio(samples, frames, channels);
                       }
                   })) {
                session->stop_desktop_capture();
                session->set_sharing_enabled(false);
                return false;
            }
        }
        session->set_system_audio_enabled(request.share_audio);

        const std::string local_peer_id = transport.local_id();
        bool retained = false;
        {
            std::scoped_lock lock(mutex);
            if (screen_requested && !sharing_active
                && screen_session == session) {
                system_audio_capture = std::move(capture);
                sharing_active = true;
                local_preview_peer_id = local_peer_id;
                retained = true;
            }
        }
        if (!retained) {
            if (capture) {
                capture->stop();
            }
            session->set_system_audio_enabled(false);
            session->set_sharing_enabled(false);
            session->stop_desktop_capture();
            return false;
        }
        transport.send_streaming_start();
        return true;
    }

    void apply_selected_audio_devices()
    {
        std::string input;
        std::string output;
        {
            std::scoped_lock lock(mutex);
            input = selected_input_device;
            output = selected_output_device;
        }

        if (!voice_runtime.set_recording_device(input)) {
            LOG_WARNING() << "Selected input audio device '" << input
                          << "' is unavailable; falling back to system default";
            if (voice_runtime.set_recording_device("default")) {
                std::scoped_lock lock(mutex);
                if (selected_input_device == input) {
                    selected_input_device = "default";
                }
            }
        }
        if (!voice_runtime.set_playout_device(output)) {
            LOG_WARNING() << "Selected output audio device '" << output
                          << "' is unavailable; falling back to system default";
            if (voice_runtime.set_playout_device("default")) {
                std::scoped_lock lock(mutex);
                if (selected_output_device == output) {
                    selected_output_device = "default";
                }
            }
        }
    }

    void start_screen_if_requested()
    {
        std::scoped_lock lifecycle_lock(screen_lifecycle_mutex);
        bool local_preview = false;
        {
            std::scoped_lock lock(mutex);
            if (!screen_requested || screen_session || !transport.connected()) {
                return;
            }
            local_preview = local_preview_enabled;
        }
        if (!screen_playout->start()) {
            LOG_WARNING()
                << "Google WebRTC screen playout device is unavailable; "
                   "screen video remains enabled";
        }
        const std::weak_ptr<Impl> weak = weak_from_this();
        const auto session_token = std::make_shared<int>(0);
        const auto recovery_scheduled = std::make_shared<std::atomic_bool>(false);
        driscord::media::ScreenSessionCallbacks session_callbacks;
        session_callbacks.on_offer = [weak](std::string sdp) {
            if (auto self = weak.lock()) {
                self->transport.send_media_offer(
                    signaling::ConnectionId::Screen, sdp);
            }
        };
        session_callbacks.on_candidate =
            [weak](std::string candidate, std::string mid) {
                if (auto self = weak.lock()) {
                    self->transport.send_media_candidate(
                        signaling::ConnectionId::Screen, candidate, mid);
                }
            };
        session_callbacks.on_remote_track =
            [weak](std::string mid, std::string, bool video) {
                if (auto self = weak.lock()) {
                    std::optional<ScreenAudioUpdate> update;
                    {
                        std::scoped_lock callback_lock(self->mutex);
                        if (video) {
                            self->screen_video_mids.insert(mid);
                        } else {
                            self->screen_audio_mids.insert(mid);
                            update = self->screen_update_locked(mid);
                        }
                    }
                    apply(std::move(update));
                }
            };
        session_callbacks.on_remote_video =
            [weak](std::string_view mid,
                driscord::media::DecodedVideoFrameView frame) {
                if (auto self = weak.lock()) {
                    self->deliver_video(mid, frame);
                }
            };
        session_callbacks.on_local_video =
            [weak](driscord::media::DecodedVideoFrameView frame) {
                if (auto self = weak.lock()) {
                    self->deliver_local_video(frame);
                }
            };
        session_callbacks.on_state =
            [weak, session_token, recovery_scheduled](
                driscord::media::ScreenConnectionState state) {
                if (state != driscord::media::MediaConnectionState::Failed
                    || recovery_scheduled->exchange(true)) {
                    return;
                }
                schedule_recovery([weak, session_token] {
                    if (auto self = weak.lock()) {
                        self->recover_screen_session(session_token);
                    }
                });
            };
        session_callbacks.on_error = [](std::string message) {
            LOG_ERROR() << "Google WebRTC screen: " << message;
        };
        auto next = std::make_shared<driscord::media::GoogleWebRtcScreenSession>(
            screen_runtime,
            driscord::media::ScreenSessionConfig {
                .remote_stream_slots = stream_defaults::kScreenReceiveSlots,
                .sharing_enabled = false,
                .system_audio_enabled = false,
                .local_preview_enabled = local_preview,
                .max_video_bitrate_bps
                = stream_defaults::kScreenVideoBitrateBps,
                .max_system_audio_bitrate_bps = stream_defaults::kSystemAudioBitrateKbps * 1000,
            },
            std::move(session_callbacks));
        if (!next->start()) {
            LOG_ERROR() << "Google WebRTC screen session failed to start";
            screen_playout->stop();
            return;
        }
        std::vector<std::string> watched;
        std::vector<ScreenAudioUpdate> updates;
        bool retained = false;
        {
            std::scoped_lock lock(mutex);
            if (screen_requested && !screen_session && transport.connected()) {
                screen_session = next;
                screen_session_token = session_token;
                watched.assign(watched_peers.begin(), watched_peers.end());
                updates = all_screen_updates_locked();
                retained = true;
            }
        }
        if (!retained) {
            next->close();
            screen_playout->stop();
            return;
        }
        apply(std::move(updates));
        // A reconnect creates a fresh server-side room/session, so targeted
        // subscriptions must be replayed even though the local watched set is
        // intentionally preserved across signaling loss.
        for (const auto& peer : watched) {
            transport.send_watch_start(peer);
        }
    }

    void stop_voice_session()
    {
        std::scoped_lock lifecycle_lock(voice_lifecycle_mutex);
        std::shared_ptr<driscord::media::GoogleWebRtcVoiceSession> old;
        {
            std::scoped_lock lock(mutex);
            old = std::move(voice_session);
            voice_session_token.reset();
            voice_bindings.clear();
        }
        if (old) {
            old->close();
        }
    }

    void stop_screen_session()
    {
        std::scoped_lock lifecycle_lock(screen_lifecycle_mutex);
        std::scoped_lock sharing_lock(sharing_mutex);
        std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> old;
        std::unique_ptr<SystemAudioCapture> old_capture;
        std::vector<std::string> removed_peers;
        std::string removed_local_preview;
        {
            std::scoped_lock lock(mutex);
            old = std::move(screen_session);
            screen_session_token.reset();
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
            removed_local_preview = std::move(local_preview_peer_id);
        }
        if (old_capture) {
            old_capture->stop();
        }
        system_audio_position = 0;
        if (old) {
            old->stop_desktop_capture();
            old->close();
        }
        screen_playout->stop();
        std::sort(removed_peers.begin(), removed_peers.end());
        removed_peers.erase(
            std::unique(removed_peers.begin(), removed_peers.end()),
            removed_peers.end());
        if (callbacks.on_frame_removed) {
            for (const auto& peer : removed_peers) {
                callbacks.on_frame_removed(peer);
            }
            if (!removed_local_preview.empty()) {
                callbacks.on_frame_removed(removed_local_preview);
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

    void deliver_local_video(
        driscord::media::DecodedVideoFrameView frame)
    {
        FrameCallback on_frame;
        std::string peer;
        {
            std::scoped_lock lock(mutex);
            if (!sharing_active || !local_preview_enabled
                || local_preview_peer_id.empty()) {
                return;
            }
            peer = local_preview_peer_id;
            on_frame = callbacks.on_frame;
        }
        if (on_frame) {
            on_frame(peer, frame.rgba.data(), frame.width, frame.height);
        }
    }

    static void apply(std::optional<VoiceTrackUpdate> update)
    {
        if (!update) {
            return;
        }
        update->session->set_remote_track_enabled(
            update->mid, update->enabled);
        if (update->volume) {
            update->session->set_remote_track_volume(
                update->mid, *update->volume);
        }
    }

    static void apply(std::vector<VoiceTrackUpdate> updates)
    {
        for (auto& update : updates) {
            apply(std::optional<VoiceTrackUpdate>(std::move(update)));
        }
    }

    static void apply(std::optional<ScreenAudioUpdate> update)
    {
        if (!update) {
            return;
        }
        update->session->set_remote_audio_enabled(
            update->mid, update->enabled);
        if (update->volume) {
            update->session->set_remote_audio_volume(
                update->mid, *update->volume);
        }
    }

    static void apply(std::vector<ScreenAudioUpdate> updates)
    {
        for (auto& update : updates) {
            apply(std::optional<ScreenAudioUpdate>(std::move(update)));
        }
    }

    std::optional<VoiceTrackUpdate> voice_update_locked(
        const std::string& mid) const
    {
        if (!voice_session) {
            return std::nullopt;
        }
        const auto binding = voice_bindings.find(mid);
        const bool bound = binding != voice_bindings.end();
        bool enabled = bound && !voice_deafened;
        float volume = master_volume_value;
        if (bound) {
            const auto muted = voice_peer_muted.find(binding->second);
            enabled = enabled
                && (muted == voice_peer_muted.end() || !muted->second);
            const auto it = voice_peer_volumes.find(binding->second);
            volume *= it == voice_peer_volumes.end() ? 1.0f : it->second;
        }
        return VoiceTrackUpdate {
            .session = voice_session,
            .mid = mid,
            .enabled = enabled,
            .volume = bound ? std::optional<double>(volume) : std::nullopt,
        };
    }

    std::vector<VoiceTrackUpdate> all_voice_updates_locked() const
    {
        std::vector<VoiceTrackUpdate> updates;
        updates.reserve(voice_bindings.size());
        for (const auto& [mid, _] : voice_bindings) {
            if (auto update = voice_update_locked(mid)) {
                updates.push_back(std::move(*update));
            }
        }
        return updates;
    }

    std::optional<ScreenAudioUpdate> screen_update_locked(
        const std::string& mid) const
    {
        if (!screen_session || !screen_audio_mids.contains(mid)) {
            return std::nullopt;
        }
        const auto binding = screen_bindings.find(mid);
        const bool enabled = binding != screen_bindings.end()
            && watched_peers.contains(binding->second);
        std::optional<double> volume;
        if (binding != screen_bindings.end()) {
            const auto it = screen_peer_volumes.find(binding->second);
            volume = it == screen_peer_volumes.end() ? 1.0f : it->second;
        }
        return ScreenAudioUpdate {
            .session = screen_session,
            .mid = mid,
            .enabled = enabled,
            .volume = volume,
        };
    }

    std::vector<ScreenAudioUpdate> all_screen_updates_locked() const
    {
        std::vector<ScreenAudioUpdate> updates;
        updates.reserve(screen_audio_mids.size());
        for (const auto& mid : screen_audio_mids) {
            if (auto update = screen_update_locked(mid)) {
                updates.push_back(std::move(*update));
            }
        }
        return updates;
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
    std::shared_ptr<GoogleWebRtcPcmPlayout> screen_playout;
    driscord::media::GoogleWebRtcRuntime voice_runtime;
    driscord::media::GoogleWebRtcRuntime screen_runtime;
    std::mutex voice_lifecycle_mutex;
    std::mutex screen_lifecycle_mutex;
    std::mutex sharing_mutex;
    mutable std::mutex mutex;
    std::shared_ptr<driscord::media::GoogleWebRtcVoiceSession> voice_session;
    std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> screen_session;
    std::shared_ptr<int> voice_session_token;
    std::shared_ptr<int> screen_session_token;
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
    std::string selected_input_device = "default";
    std::string selected_output_device = "default";
    float master_volume_value = 1.0f;
    bool voice_requested = false;
    bool voice_muted = false;
    bool voice_deafened = false;
    bool screen_requested = false;
    bool sharing_active = false;
    // What the user asked to share, kept for as long as they want to share it.
    // sharing_active only says whether a capture is running right now, so it is
    // lost whenever the screen session is torn down; without this the stream
    // would end permanently on the first transient PeerConnection failure.
    std::optional<SharingRequest> sharing_request;
    bool local_preview_enabled = false;
    std::string local_preview_peer_id;
    driscord::media::ScreenStatsTracker screen_stats;
};

GoogleWebRtcClient::GoogleWebRtcClient(Transport& transport,
    Callbacks callbacks,
    std::vector<driscord::media::IceServerConfig> ice_servers)
    : impl_(std::make_shared<Impl>(
          transport, std::move(callbacks), std::move(ice_servers)))
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
    std::shared_ptr<driscord::media::GoogleWebRtcVoiceSession> session;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->voice_muted = muted;
        session = impl_->voice_session;
    }
    if (session) {
        session->set_microphone_enabled(!muted);
    }
}

bool GoogleWebRtcClient::muted() const
{
    std::scoped_lock lock(impl_->mutex);
    return impl_->voice_muted;
}

void GoogleWebRtcClient::set_deafened(bool deafened)
{
    std::vector<Impl::VoiceTrackUpdate> updates;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->voice_deafened = deafened;
        updates = impl_->all_voice_updates_locked();
    }
    Impl::apply(std::move(updates));
    impl_->screen_playout->set_muted(deafened);
}

bool GoogleWebRtcClient::deafened() const
{
    std::scoped_lock lock(impl_->mutex);
    return impl_->voice_deafened;
}

void GoogleWebRtcClient::set_master_volume(float volume)
{
    const float clamped = clamp_volume(volume);
    std::vector<Impl::VoiceTrackUpdate> updates;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->master_volume_value = clamped;
        updates = impl_->all_voice_updates_locked();
    }
    Impl::apply(std::move(updates));
    impl_->screen_playout->set_volume(clamped);
}

float GoogleWebRtcClient::master_volume() const
{
    std::scoped_lock lock(impl_->mutex);
    return impl_->master_volume_value;
}

std::vector<driscord::media::AudioDeviceInfo>
GoogleWebRtcClient::input_devices() const
{
    return impl_->voice_runtime.recording_devices();
}

std::vector<driscord::media::AudioDeviceInfo>
GoogleWebRtcClient::output_devices() const
{
    return impl_->voice_runtime.playout_devices();
}

bool GoogleWebRtcClient::set_input_device(const std::string& id)
{
    std::scoped_lock lifecycle_lock(impl_->voice_lifecycle_mutex);
    if (!impl_->voice_runtime.set_recording_device(id)) {
        return false;
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->selected_input_device = id;
    return true;
}

bool GoogleWebRtcClient::set_output_device(const std::string& id)
{
    std::scoped_lock lifecycle_lock(impl_->voice_lifecycle_mutex);
    if (!impl_->voice_runtime.set_playout_device(id)) {
        return false;
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->selected_output_device = id;
    return true;
}

void GoogleWebRtcClient::set_peer_volume(
    const std::string& peer_id, float volume)
{
    std::vector<Impl::VoiceTrackUpdate> updates;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->voice_peer_volumes[peer_id] = clamp_volume(volume);
        updates = impl_->all_voice_updates_locked();
    }
    Impl::apply(std::move(updates));
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
    std::vector<Impl::VoiceTrackUpdate> updates;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->voice_peer_muted[peer_id] = muted;
        updates = impl_->all_voice_updates_locked();
    }
    Impl::apply(std::move(updates));
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
    stop_sharing();
    for (const auto& peer : watched) {
        impl_->transport.send_watch_stop(peer);
    }
    impl_->stop_screen_session();
}

void GoogleWebRtcClient::join_stream(const std::string& peer_id)
{
    bool inserted = false;
    std::vector<Impl::ScreenAudioUpdate> updates;
    {
        std::scoped_lock lock(impl_->mutex);
        inserted = impl_->watched_peers.insert(peer_id).second;
        if (inserted) {
            impl_->screen_stats.set_watched(peer_id, true);
        }
        updates = impl_->all_screen_updates_locked();
    }
    Impl::apply(std::move(updates));
    if (inserted) {
        impl_->transport.send_watch_start(peer_id);
    }
}

void GoogleWebRtcClient::leave_streams()
{
    std::vector<std::string> removed;
    std::vector<Impl::ScreenAudioUpdate> updates;
    {
        std::scoped_lock lock(impl_->mutex);
        removed.assign(
            impl_->watched_peers.begin(), impl_->watched_peers.end());
        impl_->watched_peers.clear();
        impl_->screen_stats.clear_watched();
        updates = impl_->all_screen_updates_locked();
    }
    Impl::apply(std::move(updates));
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
    std::vector<Impl::ScreenAudioUpdate> updates;
    {
        std::scoped_lock lock(impl_->mutex);
        removed = impl_->watched_peers.erase(peer_id) > 0;
        impl_->screen_stats.set_watched(peer_id, false);
        updates = impl_->all_screen_updates_locked();
    }
    Impl::apply(std::move(updates));
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
    std::vector<Impl::ScreenAudioUpdate> updates;
    {
        std::scoped_lock lock(impl_->mutex);
        removed = impl_->watched_peers.erase(peer_id) > 0;
        impl_->voice_peer_volumes.erase(peer_id);
        impl_->voice_peer_muted.erase(peer_id);
        impl_->screen_peer_volumes.erase(peer_id);
        impl_->screen_stats.remove_peer(peer_id);
        updates = impl_->all_screen_updates_locked();
    }
    Impl::apply(std::move(updates));
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
    const SharingRequest request {
        .selection = *selection,
        .max_width = max_width,
        .max_height = max_height,
        .fps = fps,
        .share_audio = share_audio,
    };
    if (!impl_->apply_sharing(request)) {
        return false;
    }
    std::scoped_lock lock(impl_->mutex);
    impl_->sharing_request = request;
    return true;
}

void GoogleWebRtcClient::stop_sharing()
{
    std::scoped_lock sharing_lock(impl_->sharing_mutex);
    std::unique_ptr<SystemAudioCapture> capture;
    std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> session;
    bool was_sharing = false;
    std::string removed_local_preview;
    {
        std::scoped_lock lock(impl_->mutex);
        capture = std::move(impl_->system_audio_capture);
        session = impl_->screen_session;
        was_sharing = impl_->sharing_active;
        impl_->sharing_active = false;
        // Clearing the intent as well, so recovery does not resurrect a stream
        // the user has stopped.
        impl_->sharing_request.reset();
        removed_local_preview = std::move(impl_->local_preview_peer_id);
    }
    if (session) {
        session->set_system_audio_enabled(false);
        session->set_sharing_enabled(false);
        session->stop_desktop_capture();
    }
    if (capture) {
        capture->stop();
    }
    impl_->system_audio_position = 0;
    if (was_sharing) {
        impl_->transport.send_streaming_stop();
    }
    if (!removed_local_preview.empty()
        && impl_->callbacks.on_frame_removed) {
        impl_->callbacks.on_frame_removed(removed_local_preview);
    }
}

bool GoogleWebRtcClient::sharing() const
{
    std::scoped_lock lock(impl_->mutex);
    return impl_->sharing_active;
}

void GoogleWebRtcClient::set_local_preview_enabled(bool enabled)
{
    std::scoped_lock lifecycle_lock(impl_->screen_lifecycle_mutex);
    std::shared_ptr<driscord::media::GoogleWebRtcScreenSession> session;
    std::string removed_local_preview;
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->local_preview_enabled == enabled) {
            return;
        }
        impl_->local_preview_enabled = enabled;
        session = impl_->screen_session;
        if (!enabled) {
            removed_local_preview = impl_->local_preview_peer_id;
        }
    }
    if (session) {
        session->set_local_preview_enabled(enabled);
    }
    if (!removed_local_preview.empty()
        && impl_->callbacks.on_frame_removed) {
        impl_->callbacks.on_frame_removed(removed_local_preview);
    }
}

void GoogleWebRtcClient::set_stream_volume(
    const std::string& peer_id, float volume)
{
    std::vector<Impl::ScreenAudioUpdate> updates;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->screen_peer_volumes[peer_id] = clamp_volume(volume);
        updates = impl_->all_screen_updates_locked();
    }
    Impl::apply(std::move(updates));
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
