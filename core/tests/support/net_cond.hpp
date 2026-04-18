#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace test_util {

// Packet callback type — matches Transport::ChannelSpec::on_data.
using PacketCb = std::function<void(const std::string& peer,
    const uint8_t* data,
    size_t len)>;

// ---------------------------------------------------------------------------
// NetProfile — describes a set of network impairment parameters.
// All fields use sane defaults (no impairment).
// ---------------------------------------------------------------------------
struct NetProfile {
    float loss_pct = 0.0f; // 0–100: per-packet drop probability
    uint32_t delay_ms = 0; // base one-way latency (ms)
    uint32_t jitter_ms = 0; // uniform jitter: actual delay ∈ [delay±jitter]
    float reorder_pct = 0.0f; // % of packets that receive an extra reorder gap
    uint32_t reorder_gap_ms = 20; // extra ms added to reordered packets
    float duplicate_pct = 0.0f; // % of packets that are duplicated

    static NetProfile clean()
    {
        return { };
    }

    static NetProfile degraded()
    {
        return { .loss_pct = 2.0f, .delay_ms = 30, .jitter_ms = 10 };
    }

    static NetProfile bad()
    {
        return { .loss_pct = 8.0f,
            .delay_ms = 80,
            .jitter_ms = 30,
            .reorder_pct = 5.0f,
            .reorder_gap_ms = 20 };
    }

    static NetProfile terrible()
    {
        return { .loss_pct = 15.0f,
            .delay_ms = 150,
            .jitter_ms = 60,
            .reorder_pct = 10.0f,
            .reorder_gap_ms = 20,
            .duplicate_pct = 3.0f };
    }
};

// ---------------------------------------------------------------------------
// NetworkConditioner — wraps an on_data callback and injects simulated
// network impairments (loss, latency, jitter, reorder, duplication).
//
// Intercept point: receive side only. Zero production-code changes required.
//
// Thread safety: intercept() may be called from any thread; set_profile()
// is safe to call concurrently with intercept(); the delivery loop runs on
// a dedicated background thread.
// ---------------------------------------------------------------------------
class NetworkConditioner {
public:
    struct Stats {
        uint64_t enqueued = 0;
        uint64_t dropped = 0;
        uint64_t delivered = 0;
        uint64_t reordered = 0;
        uint64_t duplicated = 0;
    };

    explicit NetworkConditioner(NetProfile profile = { })
        : profile_(std::move(profile))
        , running_(true)
    {
        delivery_thread_ = std::thread([this] { delivery_loop(); });
    }

    ~NetworkConditioner()
    {
        {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            running_ = false;
        }
        cv_.notify_all();
        if (delivery_thread_.joinable()) {
            delivery_thread_.join();
        }
    }

    NetworkConditioner(const NetworkConditioner&) = delete;
    NetworkConditioner& operator=(const NetworkConditioner&) = delete;

    // Returns a wrapped PacketCb that applies the current profile before
    // forwarding packets to real_cb. Must be called before connect() so the
    // lambda is captured at DataChannel-open time.
    PacketCb wrap(PacketCb real_cb)
    {
        real_cb_ = std::move(real_cb);
        return [this](const std::string& peer, const uint8_t* data, size_t len) {
            this->intercept(peer, data, len);
        };
    }

    // Thread-safe profile update — takes effect on the next packet.
    void set_profile(NetProfile p)
    {
        std::lock_guard<std::mutex> lk(profile_mutex_);
        profile_ = std::move(p);
    }

    Stats stats() const noexcept
    {
        return Stats {
            enqueued_.load(std::memory_order_relaxed),
            dropped_.load(std::memory_order_relaxed),
            delivered_.load(std::memory_order_relaxed),
            reordered_.load(std::memory_order_relaxed),
            duplicated_.load(std::memory_order_relaxed),
        };
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct PendingPacket {
        TimePoint deliver_at;
        std::string peer;
        std::vector<uint8_t> bytes;

        // min-heap: smallest deliver_at at top.
        bool operator>(const PendingPacket& o) const noexcept
        {
            return deliver_at > o.deliver_at;
        }
    };

    // Returns a uniform sample in [0, 100).
    float sample_pct()
    {
        std::uniform_real_distribution<float> dist(0.0f, 100.0f);
        std::lock_guard<std::mutex> lk(rng_mutex_);
        return dist(rng_);
    }

    int32_t sample_jitter(uint32_t jitter_ms)
    {
        std::uniform_int_distribution<int32_t> dist(
            -static_cast<int32_t>(jitter_ms),
            static_cast<int32_t>(jitter_ms));
        std::lock_guard<std::mutex> lk(rng_mutex_);
        return dist(rng_);
    }

    void enqueue(std::string peer,
        const uint8_t* data,
        size_t len,
        TimePoint deliver_at,
        bool is_reorder)
    {
        PendingPacket pkt;
        pkt.deliver_at = deliver_at;
        pkt.peer = std::move(peer);
        pkt.bytes.assign(data, data + len);

        {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            queue_.push(std::move(pkt));
        }
        ++enqueued_;
        if (is_reorder) {
            ++reordered_;
        }
        cv_.notify_one();
    }

    void intercept(const std::string& peer, const uint8_t* data, size_t len)
    {
        // Snapshot profile — brief hold, no allocation inside.
        NetProfile prof;
        {
            std::lock_guard<std::mutex> lk(profile_mutex_);
            prof = profile_;
        }

        // Drop?
        if (prof.loss_pct > 0.0f && sample_pct() < prof.loss_pct) {
            ++dropped_;
            return;
        }

        // Compute delivery time with optional jitter.
        uint32_t delay = prof.delay_ms;
        if (prof.jitter_ms > 0) {
            const int32_t j = sample_jitter(prof.jitter_ms);
            const int32_t adjusted = static_cast<int32_t>(delay) + j;
            delay = static_cast<uint32_t>(std::max(0, adjusted));
        }
        TimePoint deliver_at = Clock::now() + std::chrono::milliseconds(delay);

        // Reorder: add extra gap to a fraction of packets so they arrive
        // after packets that were sent later.
        bool reordered = false;
        if (prof.reorder_pct > 0.0f && sample_pct() < prof.reorder_pct) {
            deliver_at += std::chrono::milliseconds(prof.reorder_gap_ms);
            reordered = true;
        }

        enqueue(peer, data, len, deliver_at, reordered);

        // Duplicate: enqueue a second copy with a 1ms offset.
        if (prof.duplicate_pct > 0.0f && sample_pct() < prof.duplicate_pct) {
            enqueue(peer, data, len,
                deliver_at + std::chrono::milliseconds(1),
                /*is_reorder=*/false);
            ++duplicated_;
        }
    }

    void delivery_loop()
    {
        std::unique_lock<std::mutex> lk(queue_mutex_);
        while (true) {
            // Exit only when stopped AND queue is drained.
            if (!running_ && queue_.empty()) {
                break;
            }

            if (queue_.empty()) {
                cv_.wait(lk, [this] { return !running_ || !queue_.empty(); });
                continue;
            }

            const TimePoint wake_at = queue_.top().deliver_at;
            if (Clock::now() < wake_at) {
                cv_.wait_until(lk, wake_at);
                continue;
            }

            // Deliver all packets whose time has come.
            while (!queue_.empty() && Clock::now() >= queue_.top().deliver_at) {
                PendingPacket pkt = queue_.top();
                queue_.pop();
                lk.unlock();
                real_cb_(pkt.peer, pkt.bytes.data(), pkt.bytes.size());
                ++delivered_;
                lk.lock();
            }
        }
    }

    // Profile — updated via set_profile(), read in intercept().
    NetProfile profile_;
    std::mutex profile_mutex_;

    // Real downstream callback set by wrap().
    PacketCb real_cb_;

    // Delivery queue — min-heap by deliver_at.
    std::priority_queue<PendingPacket,
        std::vector<PendingPacket>,
        std::greater<PendingPacket>>
        queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    bool running_;

    // Background delivery thread.
    std::thread delivery_thread_;

    // RNG — protected by its own mutex since intercept() may be concurrent.
    std::mt19937 rng_ { std::random_device { }() };
    std::mutex rng_mutex_;

    // Atomic counters for stats().
    std::atomic<uint64_t> enqueued_ { 0 };
    std::atomic<uint64_t> dropped_ { 0 };
    std::atomic<uint64_t> delivered_ { 0 };
    std::atomic<uint64_t> reordered_ { 0 };
    std::atomic<uint64_t> duplicated_ { 0 };
};

} // namespace test_util
