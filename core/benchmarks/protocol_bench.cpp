#include <benchmark/benchmark.h>

#include "utils/chunk_assembler.hpp"
#include "utils/protocol.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

// ---- Protocol header serialize/deserialize ----

static void BM_AudioHeader_Serialize(benchmark::State& state)
{
    protocol::AudioHeader h { .seq = 12345,
        .sender_ts = utils::WallFromMs(9999999) };
    uint8_t buf[protocol::AudioHeader::kWireSize] { };

    for (auto _ : state) {
        h.serialize(buf);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_AudioHeader_Serialize);

static void BM_AudioHeader_Deserialize(benchmark::State& state)
{
    protocol::AudioHeader h { .seq = 12345,
        .sender_ts = utils::WallFromMs(9999999) };
    uint8_t buf[protocol::AudioHeader::kWireSize] { };
    h.serialize(buf);

    for (auto _ : state) {
        auto r = protocol::AudioHeader::deserialize(buf);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_AudioHeader_Deserialize);

static void BM_VideoHeader_Serialize(benchmark::State& state)
{
    protocol::VideoHeader h {
        .width = 1920,
        .height = 1080,
        .sender_ts = utils::WallFromMs(9999999),
        .bitrate_kbps = 6000,
        .frame_duration_us = 16667,
        .gop_size = 60,
    };
    uint8_t buf[protocol::VideoHeader::kWireSize] { };

    for (auto _ : state) {
        h.serialize(buf);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_VideoHeader_Serialize);

static void BM_VideoHeader_Deserialize(benchmark::State& state)
{
    protocol::VideoHeader h {
        .width = 1920,
        .height = 1080,
        .sender_ts = utils::WallFromMs(9999999),
        .bitrate_kbps = 6000,
        .frame_duration_us = 16667,
        .gop_size = 60,
    };
    uint8_t buf[protocol::VideoHeader::kWireSize] { };
    h.serialize(buf);

    for (auto _ : state) {
        auto r = protocol::VideoHeader::deserialize(buf);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_VideoHeader_Deserialize);

static void BM_ChunkHeader_Serialize(benchmark::State& state)
{
    protocol::ChunkHeader h { .frame_id = 1000, .chunk_idx = 3, .total_chunks = 10 };
    uint8_t buf[protocol::ChunkHeader::kWireSize] { };

    for (auto _ : state) {
        h.serialize(buf);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_ChunkHeader_Serialize);

static void BM_ChunkHeader_Deserialize(benchmark::State& state)
{
    protocol::ChunkHeader h { .frame_id = 1000, .chunk_idx = 3, .total_chunks = 10 };
    uint8_t buf[protocol::ChunkHeader::kWireSize] { };
    h.serialize(buf);

    for (auto _ : state) {
        auto r = protocol::ChunkHeader::deserialize(buf);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ChunkHeader_Deserialize);

// ---- chunk_frame + ChunkAssembler roundtrip ----

// Realistic frame sizes: 720p keyframe ~50KB, delta ~5KB
static void BM_ChunkAssembler_Roundtrip(benchmark::State& state)
{
    const size_t frame_size = static_cast<size_t>(state.range(0));
    constexpr size_t kPayload = 1100;

    // Pre-generate frame data
    std::vector<uint8_t> frame(frame_size);
    std::iota(frame.begin(), frame.end(), static_cast<uint8_t>(0));

    // Pre-chunk into wire packets
    std::vector<std::vector<uint8_t>> packets;
    utils::chunk_frame(0, frame.data(), frame.size(), kPayload,
        [&](const uint8_t* data, size_t len) {
            packets.emplace_back(data, data + len);
        });

    utils::ChunkAssembler assembler(kPayload);
    uint64_t fid = 0;

    for (auto _ : state) {
        // Re-stamp frame_id per iteration to avoid assembler state issues
        ++fid;
        for (auto& pkt : packets) {
            // Patch frame_id in the header
            utils::write_u64_le(pkt.data(), fid);

            assembler.push(pkt.data(), pkt.size(),
                [](uint64_t, const uint8_t* data, size_t len) {
                    benchmark::DoNotOptimize(data);
                    benchmark::DoNotOptimize(len);
                });
        }
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * frame_size);
}
BENCHMARK(BM_ChunkAssembler_Roundtrip)
    ->Arg(1100) // 1 chunk (small delta frame)
    ->Arg(5000) // ~5 chunks (typical delta)
    ->Arg(50000) // ~45 chunks (keyframe)
    ->Arg(200000); // ~182 chunks (large keyframe)

// ---- ChunkAssembler with shuffled delivery ----

static void BM_ChunkAssembler_Shuffled(benchmark::State& state)
{
    const size_t frame_size = static_cast<size_t>(state.range(0));
    constexpr size_t kPayload = 1100;

    std::vector<uint8_t> frame(frame_size);
    std::iota(frame.begin(), frame.end(), static_cast<uint8_t>(0));

    std::vector<std::vector<uint8_t>> packets;
    utils::chunk_frame(0, frame.data(), frame.size(), kPayload,
        [&](const uint8_t* data, size_t len) {
            packets.emplace_back(data, data + len);
        });

    std::mt19937 rng(42);
    utils::ChunkAssembler assembler(kPayload);
    uint64_t fid = 0;

    for (auto _ : state) {
        state.PauseTiming();
        ++fid;
        for (auto& pkt : packets) {
            utils::write_u64_le(pkt.data(), fid);
        }
        std::shuffle(packets.begin(), packets.end(), rng);
        state.ResumeTiming();

        for (auto& pkt : packets) {
            assembler.push(pkt.data(), pkt.size(),
                [](uint64_t, const uint8_t* data, size_t len) {
                    benchmark::DoNotOptimize(data);
                    benchmark::DoNotOptimize(len);
                });
        }
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * frame_size);
}
BENCHMARK(BM_ChunkAssembler_Shuffled)->Arg(5000)->Arg(50000)->Arg(200000);

// ---- chunk_frame only (sender side) ----

static void BM_ChunkFrame(benchmark::State& state)
{
    const size_t frame_size = static_cast<size_t>(state.range(0));
    constexpr size_t kPayload = 1100;

    std::vector<uint8_t> frame(frame_size);
    std::iota(frame.begin(), frame.end(), static_cast<uint8_t>(0));
    uint64_t fid = 0;

    for (auto _ : state) {
        utils::chunk_frame(++fid, frame.data(), frame.size(), kPayload,
            [](const uint8_t* data, size_t len) {
                benchmark::DoNotOptimize(data);
                benchmark::DoNotOptimize(len);
            });
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * frame_size);
}
BENCHMARK(BM_ChunkFrame)->Arg(1100)->Arg(5000)->Arg(50000)->Arg(200000);
