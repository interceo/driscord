#include "driscord_state.hpp"

DriscordState::DriscordState() {
    core.transport.on_peer_joined([this](const std::string& id) {
        fire_string(on_peer_joined, transport_mtx, id);
    });
    core.transport.on_peer_left([this](const std::string& id) {
        fire_string(on_peer_left, transport_mtx, id);
    });

    core.video_transport.on_new_streaming_peer([this](const std::string& id) {
        fire_string(on_streaming_peer, video_mtx, id);
    });
    core.video_transport.on_streaming_peer_removed([this](const std::string& id) {
        fire_string(on_streaming_peer_removed, video_mtx, id);
    });
}

DriscordState& DriscordState::get() {
    static DriscordState s;
    return s;
}
