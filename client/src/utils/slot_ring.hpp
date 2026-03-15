#pragma once

#include <array>
#include <cstdint>
#include <optional>

// Fixed-capacity ring buffer indexed by sequence number.
// NOT thread-safe — caller must synchronize.
//
// Supports out-of-order insertion and in-order extraction with gap skipping.
// O(1) push, O(1) pop in the common case (no gaps).
//
// Late packets (seq < read head) are rejected on push, preventing
// "zombie" slots that inflate size but can never be consumed.

template <typename T, size_t Capacity = 256>
class SlotRing {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t kMask = Capacity - 1;

public:
    static constexpr size_t kCapacity = Capacity;

    struct Slot {
        uint64_t seq = 0;
        bool occupied = false;
        T data{};
    };

    // ── write ────────────────────────────────────────────────────────
    // Returns true if stored.  Drops if:
    //   • seq < next_seq_ (too late — already consumed or skipped)
    //   • slot contains a packet with seq >= this one (duplicate / old)
    bool push(uint64_t seq, T data) {
        if (initialized_ && seq < next_seq_) {
            return false;
        }
        if (!initialized_) {
            next_seq_ = seq;
            initialized_ = true;
        }

        auto& slot = slots_[seq & kMask];
        if (slot.occupied) {
            if (slot.seq >= seq) {
                return false;
            }
            --size_;
        }
        slot.seq = seq;
        slot.data = std::move(data);
        slot.occupied = true;
        ++size_;
        return true;
    }

    // ── read: two-phase (peek → consume) ─────────────────────────────

    struct PeekResult {
        T* data;
        uint64_t seq;
        size_t skipped;
    };

    // Scan forward from next_seq_ for the first occupied slot.
    // Does NOT mutate state — safe to call and then decide not to consume.
    std::optional<PeekResult> peek_next() {
        if (size_ == 0) {
            return std::nullopt;
        }
        for (size_t i = 0; i < Capacity; ++i) {
            auto& s = slots_[(next_seq_ + i) & kMask];
            if (s.occupied && s.seq == next_seq_ + i) {
                return PeekResult{&s.data, s.seq, i};
            }
        }
        return std::nullopt;
    }

    struct PopResult {
        T data;
        size_t skipped;
    };

    // Remove the element found by a preceding peek_next().
    // Caller must pass the exact `skipped` value from PeekResult.
    PopResult consume_peeked(size_t skipped) {
        next_seq_ += skipped;
        auto& slot = slots_[next_seq_ & kMask];
        auto result = PopResult{std::move(slot.data), skipped};
        slot.occupied = false;
        slot.data = {};
        --size_;
        ++next_seq_;
        return result;
    }

    // ── read: one-shot (scan + remove) ───────────────────────────────

    std::optional<PopResult> pop() {
        auto p = peek_next();
        if (!p) {
            return std::nullopt;
        }
        return consume_peeked(p->skipped);
    }

    // ── queries ──────────────────────────────────────────────────────

    uint64_t next_seq() const { return next_seq_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    bool initialized() const { return initialized_; }

    void reset() {
        for (auto& s : slots_) {
            s = {};
        }
        next_seq_ = 0;
        size_ = 0;
        initialized_ = false;
    }

    template <typename F>
    void for_each_occupied(F&& fn) const {
        for (const auto& s : slots_) {
            if (s.occupied) {
                fn(s);
            }
        }
    }

private:
    std::array<Slot, Capacity> slots_{};
    uint64_t next_seq_ = 0;
    size_t size_ = 0;
    bool initialized_ = false;
};
