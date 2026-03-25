#pragma once

#include "driscord_core.hpp"
#include "transport_jni.hpp"
#include "video_transport_jni.hpp"
#include "screen_session_jni.hpp"

#include <optional>

// Global singleton that owns all core state and JNI callback adapters.
struct DriscordState {
    DriscordCore      core;
    TransportJni      transport_cbs;
    VideoTransportJni video_cbs;
    std::optional<ScreenSessionJni> screen_cbs;

    DriscordState();

    static DriscordState& get();
};
