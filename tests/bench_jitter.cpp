#include <benchmark/benchmark.h>

#include "utils/jitter.hpp"
#include "utils/slot_ring.hpp"
#include "utils/time.hpp"

#include <random>
#include <thread>

struct Payload {
    int value = 0;
};

struct BenchFrame {
    int id = 0;
    utils::WallTimestamp sender_ts{};
    bool empty() const { return false; }
};

// 1. SlotRing sequential push+pop
static void BM_SlotRing_PushPop(benchmark::State& state) {
    SlotRing<Payload, 256> ring;
    uint64_t seq = 0;
    for (auto _ : state) {
        ring.push(seq, Payload{static_cast<int>(seq)});
        auto r = ring.pop();
        benchmark::DoNotOptimize(r);
        ++seq;
    }
}
BENCHMARK(BM_SlotRing_PushPop);

// 2. SlotRing out-of-order push
static void BM_SlotRing_OutOfOrder(benchmark::State& state) {
    constexpr int BATCH = 64;
    std::vector<uint64_t> seqs(BATCH);
    std::mt19937 rng(42);

    uint64_t base = 0;
    for (auto _ : state) {
        state.PauseTiming();
        SlotRing<Payload, 256> ring;
        for (int i = 0; i < BATCH; ++i) {
            seqs[i] = base + i;
        }
        std::shuffle(seqs.begin(), seqs.end(), rng);
        state.ResumeTiming();

        for (auto s : seqs) {
            ring.push(s, Payload{static_cast<int>(s)});
        }
        for (int i = 0; i < BATCH; ++i) {
            auto r = ring.pop();
            benchmark::DoNotOptimize(r);
        }
        base += BATCH;
    }
}
BENCHMARK(BM_SlotRing_OutOfOrder);

// 3. JitterBuffer sequential push+pop (with mutex)
static void BM_JitterBuffer_PushPop(benchmark::State& state) {
    utils::JitterBuffer<int> buf(std::chrono::milliseconds(0));
    uint64_t seq = 0;

    // Prime it
    buf.push(seq++, std::make_unique<int>(0));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    buf.pop();

    for (auto _ : state) {
        buf.push(seq, std::make_unique<int>(static_cast<int>(seq)));
        auto v = buf.pop();
        benchmark::DoNotOptimize(v);
        ++seq;
    }
}
BENCHMARK(BM_JitterBuffer_PushPop);

// 4. JitterBuffer contended — multi-threaded push+pop
static void BM_JitterBuffer_Contended(benchmark::State& state) {
    static utils::JitterBuffer<int>* shared_buf = nullptr;
    static std::atomic<uint64_t> shared_seq{0};

    if (state.thread_index() == 0) {
        shared_buf = new utils::JitterBuffer<int>(std::chrono::milliseconds(0));
        shared_seq.store(0);
        // Prime
        shared_buf->push(shared_seq.fetch_add(1), std::make_unique<int>(0));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        shared_buf->pop();
    }

    for (auto _ : state) {
        if (state.thread_index() % 2 == 0) {
            uint64_t s = shared_seq.fetch_add(1, std::memory_order_relaxed);
            shared_buf->push(s, std::make_unique<int>(static_cast<int>(s)));
        } else {
            auto v = shared_buf->pop();
            benchmark::DoNotOptimize(v);
        }
    }

    if (state.thread_index() == 0) {
        delete shared_buf;
        shared_buf = nullptr;
    }
}
BENCHMARK(BM_JitterBuffer_Contended)->Threads(2)->Threads(4)->Threads(8);

// 5. Jitter full pipeline push+pop
static void BM_Jitter_PushPop(benchmark::State& state) {
    utils::Jitter<BenchFrame> j(std::chrono::milliseconds(0));
    auto base_ts = utils::WallNow();
    int id       = 0;

    // Prime
    j.push(id, BenchFrame{id++, base_ts});
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    j.pop();

    for (auto _ : state) {
        j.push(id, BenchFrame{id, base_ts + std::chrono::milliseconds(id)});
        auto v = j.pop();
        benchmark::DoNotOptimize(v);
        ++id;
    }
}
BENCHMARK(BM_Jitter_PushPop);

/*
------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations
------------------------------------------------------------------------------
BM_SlotRing_PushPop                       3.02 ns         3.01 ns    229125475
BM_SlotRing_OutOfOrder                     789 ns          787 ns       880115
BM_JitterBuffer_PushPop                   58.7 ns         58.5 ns     11853653
BM_JitterBuffer_Contended/threads:2       37.4 ns         37.3 ns     18872028
BM_JitterBuffer_Contended/threads:4       86.6 ns         86.2 ns      8134412
BM_JitterBuffer_Contended/threads:8        318 ns          316 ns      2199232
BM_Jitter_PushPop                         65.1 ns         64.8 ns     10578713
*/
