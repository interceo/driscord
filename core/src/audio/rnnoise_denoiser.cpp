#include "rnnoise_denoiser.hpp"

#include <rnnoise.h>

#include <array>

namespace {

constexpr float kNormzationFactor = 32768.0f;
constexpr float kDenormzationFactor = 1.0f / kNormzationFactor;

}

RnnoiseDenoiser::RnnoiseDenoiser()
    : state_(rnnoise_create(nullptr))
{
}

RnnoiseDenoiser::~RnnoiseDenoiser()
{
    if (state_) {
        rnnoise_destroy(state_);
    }
}

float RnnoiseDenoiser::process(const float* in, float* out)
{
    if (!state_) {
        if (out != in) {
            for (int i = 0; i < kFrameSize; ++i) {
                out[i] = in[i];
            }
        }
        return 0.0f;
    }

    std::array<float, kFrameSize> scratch_in;
    std::array<float, kFrameSize> scratch_out;
    for (int i = 0; i < kFrameSize; ++i) {
        scratch_in[i] = in[i] * kNormzationFactor;
    }
    const float vad = rnnoise_process_frame(state_, scratch_out.data(),
        scratch_in.data());
    for (int i = 0; i < kFrameSize; ++i) {
        out[i] = scratch_out[i] * kDenormzationFactor;
    }
    return vad;
}
