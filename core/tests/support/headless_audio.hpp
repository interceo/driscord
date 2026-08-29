#pragma once

#include "webrtc/google_webrtc_runtime.hpp"

namespace test_util {

inline driscord::media::GoogleWebRtcRuntimeConfig headless_audio_runtime()
{
    return {
        .injected_audio_device = driscord::media::InjectedAudioDeviceConfig { },
    };
}

}
