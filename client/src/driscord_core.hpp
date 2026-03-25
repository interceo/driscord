#pragma once

#include "transport.hpp"
#include "audio_transport.hpp"
#include "video_transport.hpp"
#include "video/screen_session.hpp"

#include <optional>
#include <string>
#include <unordered_set>

// Owns all core objects and contains business logic that coordinates them.
// Instantiated and owned by the JNI layer (DriscordState).
class DriscordCore {
public:
    Transport      transport;
    AudioTransport audio_transport;
    VideoTransport video_transport;
    std::optional<ScreenSession> screen_session;

    DriscordCore();

    // ScreenSession lifecycle — wires core objects together.
    void init_screen_session(int buf_ms, int max_sync_ms);
    void deinit_screen_session();

    // Stream watching lifecycle.
    void join_stream(const std::string& peer_id);
    void leave_stream();

    // Called when a remote peer's video stream ended.
    void on_video_peer_stream_ended(const std::string& peer_id);

private:
    std::unordered_set<std::string> watched_peers_;
};
