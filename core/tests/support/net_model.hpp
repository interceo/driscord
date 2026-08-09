#pragma once

#include "net_cond.hpp"

#include <algorithm>
#include <cstdint>
#include <queue>
#include <random>
#include <vector>

namespace test_util {

// Deterministic counterpart to NetworkConditioner: same NetProfile, but no
// threads and no wall clock. The caller supplies the current simulated time,
// so a scenario replays identically on every machine and every run.
//
// NetworkConditioner stays for integration tests, which have to impair a real
// DataChannel callback arriving on somebody else's thread.
class NetworkModel {
public:
    struct Stats {
        uint64_t sent = 0;
        uint64_t dropped = 0;
        uint64_t delivered = 0;
        uint64_t reordered = 0;
        uint64_t duplicated = 0;
    };

    NetworkModel(NetProfile profile, uint64_t seed)
        : profile_(profile)
        , rng_(seed)
    {
    }

    void set_profile(NetProfile p) { profile_ = p; }
    const NetProfile& profile() const noexcept { return profile_; }
    Stats stats() const noexcept { return stats_; }

    void send(int64_t now_us, const uint8_t* data, size_t len, uint64_t id)
    {
        ++stats_.sent;

        if (profile_.loss_pct > 0.0f && pct() < profile_.loss_pct) {
            ++stats_.dropped;
            return;
        }

        int64_t delay_us = static_cast<int64_t>(profile_.delay_ms) * 1000;
        if (profile_.jitter_ms > 0) {
            const int64_t j = static_cast<int64_t>(profile_.jitter_ms) * 1000;
            std::uniform_int_distribution<int64_t> d(-j, j);
            delay_us = std::max<int64_t>(0, delay_us + d(rng_));
        }

        bool reordered = false;
        if (profile_.reorder_pct > 0.0f && pct() < profile_.reorder_pct) {
            delay_us += static_cast<int64_t>(profile_.reorder_gap_ms) * 1000;
            reordered = true;
            ++stats_.reordered;
        }

        enqueue(now_us + delay_us, data, len, id);

        if (profile_.duplicate_pct > 0.0f && pct() < profile_.duplicate_pct) {
            enqueue(now_us + delay_us + 1000, data, len, id);
            ++stats_.duplicated;
        }
        (void)reordered;
    }

    // Hands every packet due at or before now_us to on_packet, oldest first.
    template <class F>
    void deliver_due(int64_t now_us, F&& on_packet)
    {
        while (!queue_.empty() && queue_.top().due_us <= now_us) {
            Pending p = queue_.top();
            queue_.pop();
            ++stats_.delivered;
            on_packet(p.bytes.data(), p.bytes.size(), p.id);
        }
    }

    bool empty() const noexcept { return queue_.empty(); }

private:
    struct Pending {
        int64_t due_us = 0;
        uint64_t seq = 0; // ties broken by send order, so delivery is total
        uint64_t id = 0;
        std::vector<uint8_t> bytes;

        bool operator>(const Pending& o) const noexcept
        {
            return due_us != o.due_us ? due_us > o.due_us : seq > o.seq;
        }
    };

    float pct()
    {
        std::uniform_real_distribution<float> d(0.0f, 100.0f);
        return d(rng_);
    }

    void enqueue(int64_t due_us, const uint8_t* data, size_t len, uint64_t id)
    {
        Pending p;
        p.due_us = due_us;
        p.seq = next_seq_++;
        p.id = id;
        p.bytes.assign(data, data + len);
        queue_.push(std::move(p));
    }

    NetProfile profile_;
    std::mt19937_64 rng_;
    std::priority_queue<Pending, std::vector<Pending>, std::greater<Pending>> queue_;
    uint64_t next_seq_ = 0;
    Stats stats_;
};

} // namespace test_util
