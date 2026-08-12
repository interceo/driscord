#pragma once

#include "webrtc/google_webrtc_runtime.hpp"

namespace test_util {

// WebRTC's voice engine RTC_CHECKs when the platform audio device fails to
// initialise (media/engine/adm_helpers.cc), so on a machine without a sound
// server — a CI container, a headless box — the process aborts before a test
// gets the chance to skip.
//
// Tests that need *an* audio device rather than a real one take the injected
// one instead. TestAudioDeviceModule always initialises, and its PCM is
// deterministic, which the hardware of a developer machine is not. Tests that
// deliberately exercise the platform device keep constructing the runtime
// without a config.
inline driscord::media::GoogleWebRtcRuntimeConfig headless_audio_runtime()
{
    return {
        .injected_audio_device = driscord::media::InjectedAudioDeviceConfig { },
    };
}

} // namespace test_util
