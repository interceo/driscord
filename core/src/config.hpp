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
}

// Tuning for the shared playout clock (core/src/sync) and the audio playout
// that follows it. Everything here is in the sender's microsecond timeline.
namespace sync_defaults {

// Floor and ceiling for the playout delay.
//
// The floor is three Opus frames rather than one, and that is deliberate. Two
// things only work if the buffer holds packets *beyond* the one being played:
// Opus in-band FEC, which reconstructs a lost frame from the copy carried in
// the next packet, and time-stretching, which needs the following frame as
// context for its similarity search. At one frame of delay neither can ever
// engage — the successor has not arrived yet — so the bandwidth already spent
// on FEC would be wasted and every gap would fall through to concealment.
// 60 ms is still well inside conversational latency.
//
// The ceiling stops a pathological path from pushing a call into latency
// nobody would tolerate.
inline constexpr int64_t kMinDelayUs = 60'000;
inline constexpr int64_t kMaxDelayUs = 500'000;

// Added on top of the measured delay variation so the buffer is not sitting
// exactly at the edge of underrunning.
inline constexpr int64_t kDelayMarginUs = 10'000;

// The target rises immediately when the network gets worse, but shrinks in
// small steps: dropping it quickly would mean discarding buffered audio.
inline constexpr int64_t kDelayDecayStepUs = 5'000;
inline constexpr int64_t kDelayDecayIntervalUs = 200'000;

// A sender timestamp jumping backwards by more than this means the peer's
// process restarted, so every delay estimate we hold is meaningless.
inline constexpr int64_t kSenderRestartUs = 1'000'000;

// The playout only time-stretches once the buffer is this far from target,
// and never adjusts faster than the rate below. Both keep WSOLA off the
// steady-state path, where it would colour the audio for no benefit.
inline constexpr int64_t kBufferHysteresisUs = 15'000;
inline constexpr int64_t kMaxStretchPerSecond = 50; // ms of correction / second

// Past this the playout is too far from the schedule to walk back by
// stretching — a quarter second of gradual correction would be more audible
// than one clean jump — so it re-establishes its position instead.
inline constexpr int64_t kResyncThresholdUs = 200'000;

// Screen-share audio may not run far ahead of the picture. If video stalls,
// muting a short span is less misleading and less noisy than generating Opus
// PLC while the image is frozen.
inline constexpr int64_t kMaxScreenAudioLeadUs = 30'000;

} // namespace sync_defaults
