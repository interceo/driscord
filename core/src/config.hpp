#pragma once

#include <cstddef>

namespace stream_defaults {

inline constexpr int kVoiceBitrateKbps = 64;
inline constexpr int kSystemAudioBitrateKbps = 128;
inline constexpr int kScreenVideoBitrateBps = 4'000'000;
inline constexpr std::size_t kVoiceReceiveSlots = 16;
inline constexpr std::size_t kScreenReceiveSlots = 8;

}
