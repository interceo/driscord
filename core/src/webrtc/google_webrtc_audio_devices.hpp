#pragma once

#include "webrtc/google_webrtc_runtime.hpp"

#include <string_view>
#include <vector>

namespace webrtc {
class AudioDeviceModule;
}

namespace driscord::media::detail {

// Small functional adapter over Google's index-based ADM API. Keeping this
// separate avoids turning GoogleWebRtcRuntime into an audio-device manager;
// the runtime only owns the ADM and marshals calls to its worker sequence.
[[nodiscard]] std::vector<AudioDeviceInfo> list_audio_devices(
    webrtc::AudioDeviceModule& adm, bool recording);
[[nodiscard]] bool select_audio_device(webrtc::AudioDeviceModule& adm,
    bool recording,
    std::string_view id);

} // namespace driscord::media::detail
