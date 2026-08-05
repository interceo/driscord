#include <benchmark/benchmark.h>

#include "utils/protocol.hpp"

#include <cstring>
#include <vector>

// ---- Protocol header serialize/deserialize ----

static void BM_AudioHeader_Serialize(benchmark::State& state)
{
    protocol::AudioHeader h { .seq = 12345, .flags = 0, .sender_ts_us = 9999999 };
    uint8_t buf[protocol::AudioHeader::kWireSize] { };

    for (auto _ : state) {
        h.serialize(buf);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_AudioHeader_Serialize);

static void BM_AudioHeader_Deserialize(benchmark::State& state)
{
    protocol::AudioHeader h { .seq = 12345, .flags = 0, .sender_ts_us = 9999999 };
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
        .sender_ts_us = 9999999,
        .bitrate_kbps = 6000,
        .frame_duration_us = 16667,
        .flags = 0,
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
        .sender_ts_us = 9999999,
        .bitrate_kbps = 6000,
        .frame_duration_us = 16667,
        .flags = 0,
    };
    uint8_t buf[protocol::VideoHeader::kWireSize] { };
    h.serialize(buf);

    for (auto _ : state) {
        auto r = protocol::VideoHeader::deserialize(buf);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_VideoHeader_Deserialize);

static void BM_FrameHeader_Serialize(benchmark::State& state)
{
    protocol::FrameHeader h { .frame_id = 1000 };
    uint8_t buf[protocol::FrameHeader::kWireSize] { };

    for (auto _ : state) {
        h.serialize(buf);
        benchmark::DoNotOptimize(buf);
    }
}
BENCHMARK(BM_FrameHeader_Serialize);

static void BM_FrameHeader_Deserialize(benchmark::State& state)
{
    protocol::FrameHeader h { .frame_id = 1000 };
    uint8_t buf[protocol::FrameHeader::kWireSize] { };
    h.serialize(buf);

    for (auto _ : state) {
        auto r = protocol::FrameHeader::deserialize(buf);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_FrameHeader_Deserialize);
