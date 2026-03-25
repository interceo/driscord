#include "driscord_core.hpp"
#include "utils/vector_view.hpp"

DriscordCore::DriscordCore()
    : audio_transport(transport)
    , video_transport(transport)
{}

void DriscordCore::init_screen_session(int buf_ms, int max_sync_ms) {
    screen_session.emplace(
        buf_ms,
        std::chrono::milliseconds(max_sync_ms),
        [this](const uint8_t* d, size_t l) { video_transport.send_video(d, l); },
        [this]()                            { video_transport.send_keyframe_request(); },
        [this](const uint8_t* d, size_t l) { audio_transport.send_screen_audio(d, l); }
    );
    video_transport.set_video_sink(
        [this](const std::string& peer_id, const uint8_t* data, size_t len) {
            screen_session->push_video_packet(peer_id, utils::vector_view<const uint8_t>{data, len});
        },
        [this]() {
            if (screen_session->sharing())
                screen_session->force_keyframe();
        }
    );
}

void DriscordCore::deinit_screen_session() {
    video_transport.clear_video_sink();
    screen_session.reset();
}

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
