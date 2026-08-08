#pragma once

#include <string>

struct TurnServer {
    std::string url;
    std::string user;
    std::string pass;
};

namespace stream_defaults {

inline constexpr int kVoiceBitrateKbps = 64;
inline constexpr int kSystemAudioBitrateKbps = 128;
inline constexpr int kScreenBufferMs = 120;

inline constexpr size_t kVideoSendBufferLimitBytes = 512 * 1024;

}

namespace sync_defaults {

inline constexpr int64_t kMinDelayUs = 60'000;
inline constexpr int64_t kMaxDelayUs = 500'000;

inline constexpr int64_t kDelayMarginUs = 10'000;

inline constexpr int64_t kDelayDecayStepUs = 5'000;
inline constexpr int64_t kDelayDecayIntervalUs = 200'000;

inline constexpr int64_t kSenderRestartUs = 1'000'000;

inline constexpr int64_t kBufferHysteresisUs = 15'000;
inline constexpr int64_t kMaxStretchPerSecond = 50; // ms of correction / second

inline constexpr int64_t kResyncThresholdUs = 200'000;

inline constexpr int64_t kMaxScreenAudioLeadUs = 30'000;

} // namespace sync_defaults
