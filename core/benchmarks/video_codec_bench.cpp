#include <benchmark/benchmark.h>

#include "log.hpp"
#include "video/video_codec.hpp"

#include <numeric>
#include <string>
#include <vector>

// Suppress codec init/shutdown chatter so benchmark output stays readable.
struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
static SuppressLogs suppress_logs_on_startup;

// Synthetic BGRA frame (incrementing bytes — not trivially compressible).
static std::vector<uint8_t> make_bgra(int w, int h)
{
    std::vector<uint8_t> frame(static_cast<size_t>(w) * h * 4);
    std::iota(frame.begin(), frame.end(), uint8_t { 0 });
    return frame;
}

// Pre-encode `count` frames (keyframe + P-frames) at the given resolution.
// Returns the encoded packets.  Used by decoder benchmarks so setup cost
// is not counted in the measured loop.
static std::vector<std::vector<uint8_t>> encode_stream(int w, int h, int count = 60)
{
    VideoEncoder enc;
    if (!enc.init(w, h, 30, 2000).has_value()) {
        return { };
    }
    enc.force_keyframe();
    auto src = make_bgra(w, h);
    std::vector<std::vector<uint8_t>> pkts;
    pkts.reserve(count);
    for (int i = 0; i < count * 2 && static_cast<int>(pkts.size()) < count; ++i) {
        const auto& out = enc.encode(src, w, h);
        if (!out.empty()) {
            pkts.push_back(out);
        }
    }
    return pkts;
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
BENCHMARK(BM_VideoEncoder_Encode)
    ->Args({ 640, 360 })
    ->Args({ 1280, 720 })
    ->Args({ 1920, 1080 })
    ->Args({ 3840, 2160 })
    ->Unit(benchmark::kMillisecond);

// ---- Decoder ---------------------------------------------------------------

static void BM_VideoDecoder_Decode(benchmark::State& state)
{
    const int w = static_cast<int>(state.range(0));
    const int h = static_cast<int>(state.range(1));

    // Pre-encode a realistic stream: 1 keyframe + P-frames.
    const auto stream = encode_stream(w, h, 60);
    if (stream.empty()) {
        state.SkipWithError("encoder produced no packets");
        return;
    }

    VideoDecoder dec;
    if (!dec.init()) {
        state.SkipWithError("decoder init failed");
        return;
    }

    // Warm up: feed the full pre-encoded stream once to prime the HW pipeline.
    // After this pass the decoder is in steady state (pipeline delay absorbed).
    {
        std::vector<uint8_t> rgba;
        int ow = 0, oh = 0;
        for (const auto& pkt : stream) {
            dec.decode(pkt.data(), pkt.size(), rgba, ow, oh);
        }
    }

    std::vector<uint8_t> rgba;
    int out_w = 0, out_h = 0;
    size_t idx = 0;

    // Cycle through the pre-encoded stream.  In steady state every packet
    // produces a frame (pipeline delay is already accounted for by the warm-up),
    // so this measures sustained decode + sws throughput.
    for (auto _ : state) {
        const auto& pkt = stream[idx++ % stream.size()];
        benchmark::DoNotOptimize(
            dec.decode(pkt.data(), pkt.size(), rgba, out_w, out_h));
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
    ->Args({ 3840, 2160 })
    ->Unit(benchmark::kMillisecond);

// ---- Roundtrip (encode + decode per iteration) -----------------------------

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

    // Warm up: run enough encode+decode cycles to prime both HW pipelines.
    {
        std::vector<uint8_t> rgba;
        int ow = 0, oh = 0;
        for (int i = 0; i < 60; ++i) {
            const auto& e = enc.encode(frame, w, h);
            if (!e.empty()) {
                dec.decode(e.data(), e.size(), rgba, ow, oh);
            }
        }
    }

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
    ->Args({ 3840, 2160 })
    ->Unit(benchmark::kMillisecond);

/*
--------------------------------------------------------------------------------------------
Benchmark                                  Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------
BM_VideoEncoder_Encode/640/360         0.407 ms        0.405 ms         1726 bytes_per_second=2.12008Gi/s 640x360
BM_VideoEncoder_Encode/1280/720         1.60 ms         1.59 ms          444 bytes_per_second=2.15319Gi/s 1280x720
BM_VideoEncoder_Encode/1920/1080        3.72 ms         3.70 ms          189 bytes_per_second=2.08796Gi/s 1920x1080
BM_VideoEncoder_Encode/3840/2160        15.4 ms         15.4 ms           47 bytes_per_second=2.01095Gi/s 3840x2160
BM_VideoDecoder_Decode/640/360         0.662 ms        0.548 ms         1258 bytes_per_second=1.56736Gi/s 640x360
BM_VideoDecoder_Decode/1280/720         2.42 ms         2.10 ms          336 bytes_per_second=1.63268Gi/s 1280x720
BM_VideoDecoder_Decode/1920/1080        5.30 ms         4.66 ms          149 bytes_per_second=1.65757Gi/s 1920x1080
BM_VideoDecoder_Decode/3840/2160        20.9 ms         18.8 ms           37 bytes_per_second=1.64599Gi/s 3840x2160
BM_VideoCodec_Roundtrip/640/360         1.21 ms        0.993 ms          685 bytes_per_second=884.7Mi/s 640x360
BM_VideoCodec_Roundtrip/1280/720        4.31 ms         3.87 ms          182 bytes_per_second=909.067Mi/s 1280x720
BM_VideoCodec_Roundtrip/1920/1080       9.39 ms         8.60 ms           79 bytes_per_second=919.469Mi/s 1920x1080
BM_VideoCodec_Roundtrip/3840/2160       36.9 ms         34.3 ms           20 bytes_per_second=922.576Mi/s 3840x2160
*/