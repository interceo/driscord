#pragma once

#include <array>
#include <cstdint>
#include <optional>

template <typename T, size_t Capacity = 256> class SlotRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t kMask = Capacity - 1;

public:
    struct Slot {
        uint64_t seq = UINT64_MAX;
        T data;
    };

    struct PeekResult {
        T* data;
        uint64_t seq;
        size_t skipped;
    };

    struct PopResult {
        T data;
        size_t skipped;
    };

    template <class U> inline bool push(const uint64_t seq, U&& data) {
        if (initialized_ && seq < next_seq_) [[unlikely]] {
            return false;
        }

        if (!initialized_) [[unlikely]] {
            next_seq_    = seq;
            initialized_ = true;
        }

        auto& slot = slots_[seq & kMask];

        if (slot.seq != UINT64_MAX && slot.seq >= seq) [[unlikely]] {
            return false;
        }

        if (slot.seq != UINT64_MAX) {
            --size_;
        }

        slot.seq  = seq;
        slot.data = std::move(data);

        ++size_;
        return true;
    }

    std::optional<PeekResult> peek_next() {
        if (size_ == 0) {
            return std::nullopt;
        }

        auto& slot = slots_[next_seq_ & kMask];
        if (slot.seq == next_seq_) [[likely]] {
            return PeekResult{&slot.data, next_seq_, 0};
        }

        for (size_t i = 1; i < Capacity; ++i) {
            auto& s = slots_[(next_seq_ + i) & kMask];
            if (s.seq == next_seq_ + i) {
                return PeekResult{&s.data, s.seq, i};
            }
        }

        return std::nullopt;
    }

    PopResult consume_peeked(const size_t skipped) {
        next_seq_ += skipped;

        auto& slot = slots_[next_seq_ & kMask];

        PopResult result{
            std::move(slot.data),
            skipped,
        };

        slot.data = {};
        slot.seq  = UINT64_MAX;

        --size_;
        ++next_seq_;

        return result;
    }

    std::optional<PopResult> pop() {
        auto p = peek_next();
        if (!p) {
            return std::nullopt;
        }

        return consume_peeked(p->skipped);
    }

    void advance_seq() {
        if (initialized_) {
            ++next_seq_;
        }
    }

    uint64_t next_seq() const { return next_seq_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    bool initialized() const { return initialized_; }

    void reset() {
        for (auto& s : slots_) {
            s.seq = UINT64_MAX;
        }

        next_seq_    = 0;
        size_        = 0;
        initialized_ = false;
    }

    template <typename F> void for_each_occupied(F&& fn) const {
        for (const auto& s : slots_) {
            if (s.seq != UINT64_MAX) {
                fn(s);
            }
        }
    }

private:
    std::array<Slot, Capacity> slots_;

    uint64_t next_seq_ = 0;
    size_t size_       = 0;
    bool initialized_  = false;
};