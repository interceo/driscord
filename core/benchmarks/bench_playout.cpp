#include <benchmark/benchmark.h>

#include "audio/wsola.hpp"
#include "sync/delay_estimator.hpp"
#include "sync/media_clock.hpp"
#include "utils/reorder_buffer.hpp"

#include <cmath>
#include <numbers>
#include <vector>

namespace {

struct Payload {
    uint16_t len = 0;
    int64_t ts = 0;
    std::array<uint8_t, 512> data { };
};

constexpr int kRate = 48000;

std::vector<float> tone(size_t frames)
{
    std::vector<float> v(frames);
    for (size_t i = 0; i < frames; ++i) {
        v[i] = 0.4f * std::sin(2.0f * std::numbers::pi_v<float> * 220.0f * static_cast<float>(i) / kRate);
    }
    return v;
}

} // namespace

// --- ReorderBuffer -----------------------------------------------------------

static void BM_ReorderBuffer_InOrder(benchmark::State& state)
{
    utils::ReorderBuffer<Payload, 128> buf;
    uint64_t seq = 0;
    for (auto _ : state) {
        buf.push(seq, Payload { });
        benchmark::DoNotOptimize(buf.peek(seq));
        buf.advance_base_to(seq + 1);
        ++seq;
    }
}
BENCHMARK(BM_ReorderBuffer_InOrder);

static void BM_ReorderBuffer_OutOfOrder(benchmark::State& state)
{
    for (auto _ : state) {
        state.PauseTiming();
        utils::ReorderBuffer<Payload, 128> buf;
        state.ResumeTiming();

        // Deliver 64 packets in reverse, then drain them in order.
        for (int i = 63; i >= 0; --i) {
            buf.push(static_cast<uint64_t>(i), Payload { });
        }
        for (uint64_t s = 0; s < 64; ++s) {
            if (buf.contains(s)) {
                benchmark::DoNotOptimize(buf.peek(s));
            }
            buf.advance_base_to(s + 1);
        }
    }
}
BENCHMARK(BM_ReorderBuffer_OutOfOrder);

// Finding the packet on the far side of a hole is the operation the audio
// callback runs on every lost frame — it must not depend on the hole's size.
static void BM_ReorderBuffer_GapScan(benchmark::State& state)
{
    const uint64_t gap = static_cast<uint64_t>(state.range(0));
    utils::ReorderBuffer<Payload, 128> buf;
    buf.push(0, Payload { });
    buf.take(0);
    buf.advance_base_to(1);
    buf.push(gap, Payload { });

    for (auto _ : state) {
        benchmark::DoNotOptimize(buf.next_present());
    }
}
BENCHMARK(BM_ReorderBuffer_GapScan)->Arg(1)->Arg(16)->Arg(64)->Arg(120);

// --- Delay estimation --------------------------------------------------------

static void BM_DelayEstimator_Observe(benchmark::State& state)
{
    avsync::DelayEstimator e;
    int64_t owd = 10'000;
    for (auto _ : state) {
        e.observe(owd);
        owd = 10'000 + (owd * 7919) % 50'000;
    }
}
BENCHMARK(BM_DelayEstimator_Observe);

static void BM_DelayEstimator_Percentile(benchmark::State& state)
{
    avsync::DelayEstimator e;
    for (int i = 0; i < 512; ++i) {
        e.observe(10'000 + (i * 137) % 40'000);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(e.p95_variation_us());
    }
}
BENCHMARK(BM_DelayEstimator_Percentile);

// --- MediaClock --------------------------------------------------------------

// Runs on every arriving packet, on the network thread.
static void BM_MediaClock_Observe(benchmark::State& state)
{
    avsync::MediaClock c;
    int64_t ts = 0;
    for (auto _ : state) {
        c.observe(avsync::MediaClock::Stream::Audio, ts, ts + 10'000);
        ts += 20'000;
    }
}
BENCHMARK(BM_MediaClock_Observe);

// Runs inside the audio callback and the render tick; must stay atomic-only.
static void BM_MediaClock_Deadline(benchmark::State& state)
{
    avsync::MediaClock c;
    for (int i = 0; i < 512; ++i) {
        c.observe(avsync::MediaClock::Stream::Audio,
            i * 20'000, i * 20'000 + 10'000);
    }
    int64_t ts = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(c.deadline_us(ts));
        ts += 20'000;
    }
}
BENCHMARK(BM_MediaClock_Deadline);

// Both streams publishing while the audio callback reads.
static void BM_MediaClock_Contended(benchmark::State& state)
{
    static avsync::MediaClock* shared = nullptr;
    if (state.thread_index() == 0) {
        shared = new avsync::MediaClock();
        shared->set_video_active(true);
    }

    int64_t ts = state.thread_index() * 1'000'000;
    const auto stream = (state.thread_index() % 2) == 0
        ? avsync::MediaClock::Stream::Audio
        : avsync::MediaClock::Stream::Video;

    for (auto _ : state) {
        shared->observe(stream, ts, ts + 10'000);
        benchmark::DoNotOptimize(shared->deadline_us(ts));
        ts += 20'000;
    }

    if (state.thread_index() == 0) {
        delete shared;
        shared = nullptr;
    }
}
BENCHMARK(BM_MediaClock_Contended)->Threads(2)->Threads(4);

// --- WSOLA ------------------------------------------------------------------

static void BM_Wsola_Compress(benchmark::State& state)
{
    Wsola w(kRate, 1);
    const auto in = tone(1920);
    std::vector<float> out(1920 + w.max_lag());
    for (auto _ : state) {
        benchmark::DoNotOptimize(w.compress(in.data(), in.size(), out.data()));
    }
}
BENCHMARK(BM_Wsola_Compress);

static void BM_Wsola_Expand(benchmark::State& state)
{
    Wsola w(kRate, 1);
    const auto in = tone(1920);
    std::vector<float> out(1920 + w.max_lag());
    for (auto _ : state) {
        benchmark::DoNotOptimize(w.expand(in.data(), in.size(), out.data(), out.size()));
    }
}
BENCHMARK(BM_Wsola_Expand);

/*
Reference results (Release, -O2, x86-64, 12 x 3.3 GHz):

    BM_ReorderBuffer_InOrder                19.9 ns
    BM_ReorderBuffer_OutOfOrder             1894 ns
    BM_ReorderBuffer_GapScan/1              7.17 ns
    BM_ReorderBuffer_GapScan/16             7.16 ns
    BM_ReorderBuffer_GapScan/64             7.66 ns
    BM_ReorderBuffer_GapScan/120            7.66 ns
    BM_DelayEstimator_Percentile            14.1 ns
    BM_MediaClock_Observe                   11.1 ns
    BM_MediaClock_Deadline                 0.242 ns
    BM_MediaClock_Contended/threads:2        152 ns
    BM_MediaClock_Contended/threads:4        568 ns
    BM_Wsola_Compress                      16677 ns
    BM_Wsola_Expand                        16640 ns

Two of these are the point of the design:

GapScan is flat across gap sizes. Finding the packet past a hole is what the
audio callback does on every lost frame, and it now costs the same whether one
packet is missing or a hundred — the occupancy bitmask replaced a linear walk
over every slot.

MediaClock_Deadline is a quarter of a nanosecond because it is two relaxed
atomic loads and an add. Everything expensive about delay estimation happens on
the network thread, so the real-time path only ever reads the answer.

WSOLA is the one genuinely costly operation, which is why the playout gates it
behind a hysteresis band and a token bucket instead of running it per frame.
The coarse-to-fine search brought it down from 116 us.
*/
