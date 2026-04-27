#pragma once

struct DenoiseState;

// Thin RAII wrapper around xiph/rnnoise. Operates on a single 10 ms mono
// frame at 48 kHz (480 samples). The capture pipeline collects 20 ms Opus
// frames, so it runs the denoiser twice per encode.
class RnnoiseDenoiser {
public:
    static constexpr int kFrameSize = 480; // 10 ms @ 48 kHz mono

    RnnoiseDenoiser();
    ~RnnoiseDenoiser();

    RnnoiseDenoiser(const RnnoiseDenoiser&) = delete;
    RnnoiseDenoiser& operator=(const RnnoiseDenoiser&) = delete;

    // Denoise one kFrameSize-sample frame. `in` and `out` may alias.
    // Inputs are normalized float [-1, 1] (miniaudio ma_format_f32);
    // RNNoise expects ±32768, scaling happens internally.
    // Returns the VAD speech probability in [0, 1].
    float process(const float* in, float* out);

private:
    DenoiseState* state_ = nullptr;
};
