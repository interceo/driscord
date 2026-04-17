#include "net_cond.hpp"

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using test_util::NetProfile;
using test_util::NetworkConditioner;

// ---------------------------------------------------------------------------
// BM_Conditioner_ZeroLoss_Throughput
//
// Measures raw conditioner overhead when the profile has no impairments:
// every packet passes through immediately (no delay queue sleep).
// ---------------------------------------------------------------------------
static void BM_Conditioner_ZeroLoss_Throughput(benchmark::State& state)
{
    std::atomic<int64_t> received { 0 };
    NetworkConditioner cond(NetProfile::clean());

    auto wrapped = cond.wrap([&received](const std::string&,
                                 const uint8_t*, size_t) {
        ++received;
    });

    std::vector<uint8_t> payload(64, 0xAB);
    const std::string peer = "bench-peer";

    for (auto _ : state) {
        received.store(0, std::memory_order_relaxed);
        const int64_t n = state.range(0);

        for (int64_t i = 0; i < n; ++i) {
            wrapped(peer, payload.data(), payload.size());
        }

        // Wait for all packets to be delivered by the background thread.
        while (received.load(std::memory_order_relaxed) < n) {
            std::this_thread::yield();
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Conditioner_ZeroLoss_Throughput)->Arg(1000)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// BM_Conditioner_50msDelay_Accuracy
//
// Sends 1000 packets with a 50ms base delay and measures the wall-clock
// accuracy of the conditioner's delivery timing.
// Asserts that total wall time for all deliveries is within ±15ms of the
// expected 50ms (measured from first send to last receive).
// ---------------------------------------------------------------------------
static void BM_Conditioner_50msDelay_Accuracy(benchmark::State& state)
{
    using Clock = std::chrono::steady_clock;

    std::atomic<int64_t> received { 0 };
    NetworkConditioner cond(NetProfile { .delay_ms = 50 });

    Clock::time_point first_send;
    Clock::time_point last_receive;

    auto wrapped = cond.wrap([&received, &last_receive](const std::string&,
                                 const uint8_t*, size_t) {
        last_receive = Clock::now();
        ++received;
    });

    std::vector<uint8_t> payload(64, 0xBC);
    const std::string peer = "bench-peer";

    for (auto _ : state) {
        received.store(0, std::memory_order_relaxed);
        const int64_t n = state.range(0);

        first_send = Clock::now();
        for (int64_t i = 0; i < n; ++i) {
            wrapped(peer, payload.data(), payload.size());
        }

        // Wait for the last delivery (50ms delay + margin).
        while (received.load(std::memory_order_relaxed) < n) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        const auto elapsed_ms
            = std::chrono::duration_cast<std::chrono::milliseconds>(
                last_receive - first_send)
                  .count();

        // Record actual elapsed time as a custom counter for inspection.
        state.counters["last_deliver_ms"] = static_cast<double>(elapsed_ms);

        // Soft assertion: delivery should land within 50±15ms of first send.
        if (elapsed_ms < 35 || elapsed_ms > 65) {
            state.SkipWithError("Delivery timing outside ±15ms tolerance");
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Conditioner_50msDelay_Accuracy)->Arg(1000)->Unit(benchmark::kMillisecond);
