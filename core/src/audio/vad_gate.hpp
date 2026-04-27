#pragma once

// Hysteresis-based voice activity gate.
//
// Drives mic-send on/off from a per-frame VAD probability (e.g. the value
// returned by RnnoiseDenoiser::process). Uses two thresholds plus a hangover
// to avoid chattering at the start/end of speech:
//
//   Closed   --(prob >= open, N frames)--> Open
//   Open     --(prob <= close)----------->  Hangover
//   Hangover --(prob >= open)------------->  Open
//   Hangover --(silence > hangover_ms)--->  Closed
//
// Methods are NOT thread-safe — owned and called from a single thread
// (the audio capture callback).
class VadGate {
public:
    enum class State { Closed, Opening, Open, Hangover };

    void set_thresholds(float open, float close);
    void set_open_frames(int n);
    void set_hangover_ms(int ms);

    void reset();

    // Feeds one frame of duration `frame_duration_ms`. Returns true when the
    // gate is open and the frame should be sent downstream.
    bool update(float prob, int frame_duration_ms);

    State state() const noexcept { return state_; }

private:
    State state_ = State::Closed;
    float open_threshold_ = 0.6f;
    float close_threshold_ = 0.3f;
    int open_frames_required_ = 2;
    int opening_count_ = 0;
    int hangover_ms_ = 200;
    int hangover_remaining_ms_ = 0;
};
