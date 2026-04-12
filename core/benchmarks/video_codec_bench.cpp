#include <benchmark/benchmark.h>

#include "video/video_codec.hpp"

#include <numeric>
#include <string>
#include <vector>

// Synthetic BGRA frame (incrementing bytes so it's not trivially compressible).
static std::vector<uint8_t> make_bgra(int w, int h)
{
    std::vector<uint8_t> frame(static_cast<size_t>(w) * h * 4);
    std::iota(frame.begin(), frame.end(), uint8_t { 0 });
    return frame;
}

// Pre-encode a keyframe at the given resolution. Used by decoder benchmarks
// so they don't include encoder overhead in their measurement.
static std::vector<uint8_t> encode_keyframe(int w, int h)
{
    VideoEncoder enc;
    if (!enc.init(w, h, 30, 2000).has_value()) {
        return { };
    }
    enc.force_keyframe();
    auto src = make_bgra(w, h);
    for (int i = 0; i < 10; ++i) {
        const auto& out = enc.encode(src, w, h);
        if (!out.empty()) {
            return out;
        }
    }
    return { };
}

// ---- Encoder ---------------------------------------------------------------

static void BM_VideoEncoder_Encode(benchmark::State& state)
{
    const int w = static_cast<int>(state.range(0));
    const int h = static_cast<int>(state.range(1));

    VideoEncoder enc;
    if (!enc.init(w, h, 30, 2000).has_value()) {
        state.SkipWithError("encoder init failed");
        return;
    }

    auto frame = make_bgra(w, h);
    enc.force_keyframe();

    for (auto _ : state) {
        const auto& out = enc.encode(frame, w, h);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<int64_t>(state.iterations()) * w * h * 4);
    state.SetLabel(std::to_string(w) + "x" + std::to_string(h));
}
// 640×360 ≈ typical window share; 1280×720 and 1920×1080 for full screen.
BENCHMARK(BM_VideoEncoder_Encode)
    ->Args({ 640, 360 })
    ->Args({ 1280, 720 })
    ->Args({ 1920, 1080 })
    ->Unit(benchmark::kMillisecond);

// ---- Decoder ---------------------------------------------------------------

static void BM_VideoDecoder_Decode(benchmark::State& state)
{
    const int w = static_cast<int>(state.range(0));
    const int h = static_cast<int>(state.range(1));

    VideoDecoder dec;
    if (!dec.init()) {
        state.SkipWithError("decoder init failed");
        return;
    }

    const auto encoded = encode_keyframe(w, h);
    if (encoded.empty()) {
        state.SkipWithError("could not produce encoded keyframe");
        return;
    }

    // Warm up: one decode to initialise internal decoder state (sws context,
    // HW surfaces, etc.) before the timed loop starts.
    {
        std::vector<uint8_t> rgba;
        int ow = 0, oh = 0;
        dec.decode(encoded.data(), encoded.size(), rgba, ow, oh);
    }

    std::vector<uint8_t> rgba;
    int out_w = 0, out_h = 0;

    // A keyframe is self-contained: re-sending it to the same decoder context
    // is valid and measures pure decode + sws throughput.
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            dec.decode(encoded.data(), encoded.size(), rgba, out_w, out_h));
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<int64_t>(state.iterations()) * w * h * 4);
    state.SetLabel(std::to_string(w) + "x" + std::to_string(h));
}
BENCHMARK(BM_VideoDecoder_Decode)
    ->Args({ 640, 360 })
    ->Args({ 1280, 720 })
    ->Args({ 1920, 1080 })
    ->Unit(benchmark::kMillisecond);

// ---- Roundtrip (encode + decode in one iteration) --------------------------

static void BM_VideoCodec_Roundtrip(benchmark::State& state)
{
    const int w = static_cast<int>(state.range(0));
    const int h = static_cast<int>(state.range(1));

    VideoEncoder enc;
    if (!enc.init(w, h, 30, 2000).has_value()) {
        state.SkipWithError("encoder init failed");
        return;
    }

    VideoDecoder dec;
    if (!dec.init()) {
        state.SkipWithError("decoder init failed");
        return;
    }

    auto frame = make_bgra(w, h);
    enc.force_keyframe();

    std::vector<uint8_t> rgba;
    int out_w = 0, out_h = 0;

    for (auto _ : state) {
        const auto& encoded = enc.encode(frame, w, h);
        if (!encoded.empty()) {
            benchmark::DoNotOptimize(
                dec.decode(encoded.data(), encoded.size(), rgba, out_w, out_h));
        }
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(
        static_cast<int64_t>(state.iterations()) * w * h * 4);
    state.SetLabel(std::to_string(w) + "x" + std::to_string(h));
}
BENCHMARK(BM_VideoCodec_Roundtrip)
    ->Args({ 640, 360 })
    ->Args({ 1280, 720 })
    ->Args({ 1920, 1080 })
    ->Unit(benchmark::kMillisecond);
