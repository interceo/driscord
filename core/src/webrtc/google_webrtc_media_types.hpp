#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace driscord::media {

enum class MediaConnectionState {
    New,
    Connecting,
    Connected,
    Disconnected,
    Failed,
    Closed,
};

using VoiceConnectionState = MediaConnectionState;
using ScreenConnectionState = MediaConnectionState;

struct DecodedAudioFrameView {
    // Valid only for the duration of its callback. Samples are interleaved.
    std::span<const int16_t> samples;
    int sample_rate_hz = 0;
    size_t channels = 0;
    size_t frames = 0;
};

} // namespace driscord::media
