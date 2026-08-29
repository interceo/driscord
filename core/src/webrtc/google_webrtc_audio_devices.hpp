#pragma once

#include "webrtc/google_webrtc_runtime.hpp"

#include <string_view>
#include <vector>

namespace webrtc {
class AudioDeviceModule;
}

namespace driscord::media::detail {

[[nodiscard]] std::vector<AudioDeviceInfo> list_audio_devices(
    webrtc::AudioDeviceModule& adm, bool recording);
[[nodiscard]] bool select_audio_device(webrtc::AudioDeviceModule& adm,
    bool recording,
    std::string_view id);

}
