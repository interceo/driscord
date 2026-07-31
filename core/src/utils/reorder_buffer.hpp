#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace utils {

enum class PushResult {
    Stored,
    Duplicate,
    // Older than the read cursor: the consumer has already moved past it.
    TooLate,
    // So far ahead that it cannot share the window with the current cursor.
    // The buffer dropped everything and restarted at this sequence number.
    Resynced,
};

// Sequence-indexed reordering window.
//
// Packets arrive out of order, duplicated, or not at all; this holds them by
// sequence number until the consumer's cursor reaches them. It is a pure data
// structure: it knows nothing about time, allocates nothing, and takes no
// locks — the owner supplies whatever synchronization its threads need.
//
// A sequence number `s` is addressable while `base_seq() <= s < base_seq() +
// Capacity`. Slot reuse is safe because that window is exactly Capacity wide,
// so two live sequence numbers can never map to the same slot.
//
// Occupancy lives in a bitmask rather than in the slots themselves, which is
// what makes next_present() — "where does the next packet after this gap
// start?" — a couple of word scans instead of a walk over every slot.
template <typename T, size_t Capacity = 128>
class ReorderBuffer {
    static_assert(Capacity >= 64, "Capacity must cover at least one mask word");
    static_assert((Capacity & (Capacity - 1)) == 0,
        "Capacity must be a power of 2");

    static constexpr size_t kIndexMask = Capacity - 1;
    static constexpr size_t kWords = Capacity / 64;

public:
    static constexpr size_t kCapacity = Capacity;

    PushResult push(const uint64_t seq, T&& value)
    {
        if (!started_) [[unlikely]] {
            started_ = true;
            base_ = seq;
        } else if (seq < base_) [[unlikely]] {
            // Before anything has been read, the cursor is only a guess made
            // from whichever packet happened to arrive first. An earlier one
            // showing up means the guess was wrong, and nothing has been
            // played yet, so move the cursor back instead of dropping it.
            if (consumed_ || base_ - seq >= Capacity) {
                return PushResult::TooLate;
            }
            base_ = seq;
        } else if (seq - base_ >= Capacity) [[unlikely]] {
            clear_slots();
            base_ = seq;
            store(seq, std::move(value));
            return PushResult::Resynced;
        }

        if (test(seq)) [[unlikely]] {
            return PushResult::Duplicate;
        }

        store(seq, std::move(value));
        return PushResult::Stored;
    }

    // Sequence number the consumer will read next. Everything below it has
    // been taken or skipped.
    uint64_t base_seq() const noexcept { return base_; }

    bool contains(const uint64_t seq) const noexcept
    {
        return in_window(seq) && test(seq);
    }

    // Lowest present sequence number >= `from`, or nullopt if the rest of the
    // window is empty. Used both to detect a gap (result > base_seq()) and to
    // find the packet just past it, which is what Opus in-band FEC needs.
    std::optional<uint64_t> next_present(uint64_t from) const noexcept
    {
        if (!started_ || count_ == 0) {
            return std::nullopt;
        }
        if (from < base_) {
            from = base_;
        }
        const uint64_t end = base_ + Capacity;
        if (from >= end) {
            return std::nullopt;
        }

        const size_t span = static_cast<size_t>(end - from);
        size_t idx = static_cast<size_t>(from & kIndexMask);
        size_t scanned = 0;

        while (scanned < span) {
            const size_t bit = idx & 63u;
            const uint64_t word = occupied_[idx >> 6] >> bit;
            if (word != 0) {
                const size_t offset = static_cast<size_t>(std::countr_zero(word));
                if (scanned + offset >= span) {
                    return std::nullopt;
                }
                return from + scanned + offset;
            }
            const size_t step = 64u - bit;
            scanned += step;
            idx = (idx + step) & kIndexMask;
        }
        return std::nullopt;
    }

    std::optional<uint64_t> next_present() const noexcept
    {
        return next_present(base_);
    }

    // Borrow without consuming. Valid until the next push or advance.
    const T* peek(const uint64_t seq) const noexcept
    {
        return contains(seq) ? &slots_[seq & kIndexMask] : nullptr;
    }

    // Consume. Precondition: contains(seq).
    T take(const uint64_t seq) noexcept
    {
        consumed_ = true;
        T out = std::move(slots_[seq & kIndexMask]);
        clear(seq);
        --count_;
        return out;
    }

    // Move the read cursor to `seq`, discarding anything still held below it.
    // Skipping over an absent sequence number is how the consumer accounts for
    // a packet it concealed rather than played.
    void advance_base_to(const uint64_t seq) noexcept
    {
        if (!started_ || seq <= base_) {
            return;
        }
        consumed_ = true;
        const uint64_t target = seq - base_ >= Capacity ? base_ + Capacity : seq;
        for (uint64_t s = base_; s < target; ++s) {
            if (test(s)) {
                clear(s);
                slots_[s & kIndexMask] = T { };
                --count_;
            }
        }
        base_ = seq;
    }

    size_t size() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }
    bool started() const noexcept { return started_; }

    void reset() noexcept
    {
        clear_slots();
        started_ = false;
        consumed_ = false;
        base_ = 0;
    }

private:
    bool in_window(const uint64_t seq) const noexcept
    {
        return started_ && seq >= base_ && seq - base_ < Capacity;
    }

    bool test(const uint64_t seq) const noexcept
    {
        const size_t idx = static_cast<size_t>(seq & kIndexMask);
        return (occupied_[idx >> 6] >> (idx & 63u)) & 1u;
    }

    void set(const uint64_t seq) noexcept
    {
        const size_t idx = static_cast<size_t>(seq & kIndexMask);
        occupied_[idx >> 6] |= uint64_t { 1 } << (idx & 63u);
    }

    void clear(const uint64_t seq) noexcept
    {
        const size_t idx = static_cast<size_t>(seq & kIndexMask);
        occupied_[idx >> 6] &= ~(uint64_t { 1 } << (idx & 63u));
    }

    void store(const uint64_t seq, T&& value)
    {
        slots_[seq & kIndexMask] = std::move(value);
        set(seq);
        ++count_;
    }

    // Releases whatever the occupied slots hold — for payload types that own
    // heap memory, leaving them in place would pin a full window of frames.
    void clear_slots() noexcept
    {
        if (count_ != 0) {
            for (uint64_t s = base_; s < base_ + Capacity; ++s) {
                if (test(s)) {
                    slots_[s & kIndexMask] = T { };
                }
            }
        }
        occupied_.fill(0);
        count_ = 0;
    }

    std::array<T, Capacity> slots_ { };
    std::array<uint64_t, kWords> occupied_ { };
    uint64_t base_ = 0;
    size_t count_ = 0;
    bool started_ = false;
    bool consumed_ = false; // gates the start-of-stream cursor rewind
};

} // namespace utils
