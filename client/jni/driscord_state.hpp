#pragma once

#include "transport_jni.hpp"
#include "audio_transport_jni.hpp"
#include "video_transport_jni.hpp"

// Global singleton that owns Transport, AudioTransport, VideoTransport.
// Initialized once on first access; lives for the duration of the process.
struct DriscordState {
    TransportJni      transport;
    AudioTransportJni audio_transport;
    VideoTransportJni video_transport;

    DriscordState()
        : audio_transport(transport)
        , video_transport(transport)
    {}

    static DriscordState& get();
};
