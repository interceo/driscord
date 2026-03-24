#pragma once

#include "transport_jni.hpp"
#include "audio_transport_jni.hpp"
#include "video_transport_jni.hpp"
#include "screen_session_jni.hpp"

#include <optional>

// Global singleton that owns Transport, AudioTransport, VideoTransport, ScreenSession.
// Initialized once on first access; lives for the duration of the process.
// screen_session is emplaced lazily via NativeScreenSession.init().
struct DriscordState {
    TransportJni      transport;
    AudioTransportJni audio_transport;
    VideoTransportJni video_transport;
    std::optional<ScreenSessionJni> screen_session;

    DriscordState()
        : audio_transport(transport)
        , video_transport(transport)
    {}

    static DriscordState& get();
};
