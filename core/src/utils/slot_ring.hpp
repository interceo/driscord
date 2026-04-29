#pragma once

#include <array>
#include <cstdint>
#include <optional>

enum class PushStatus { Stored,
    Overwritten,
    Late,
};

template <typename T, size_t Capacity = 256>
class SlotRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
        "Capacity must be a power of 2");
    static constexpr size_t kMask = Capacity - 1;

public:
    struct Slot {
        uint64_t slot_id = UINT64_MAX;
        T data;

        bool emtpy() const noexcept
        {
            return slot_id != UINT64_MAX;
        }
    };

    template <class U>
    inline void push(const uint64_t slot_id, U&& data)
    {
        Slot& slot = ring[slot_id & kMask];

        if (slot.slot_id == end_id) [[unlikely]] {
            ++end_id;
        }

        slot.slot_id = slot_id;
        slot.data = std::move(data);

        ++end_id;
    }

    std::optional<T> pop()
    {
        Slot& slot = ring[end_id & kMask];
        if (slot.emtpy()) {
            return std::nullopt;
        }

        ++start_id;

        return std::make_optional<T>(std::move(slot.data));
    }

    void advance_seq(const size_t n = 1)
    {
        const auto skipped_slots = std::min(n, size());
        start_id += n;
    }

    uint64_t next_seq() const { return end_id; }
    size_t size() const { return end_id - start_id; }
    bool empty() const { return start_id == 0; }

    void reset()
    {
        ring.fill(Slot { });
        end_id = 0;
        start_id = 0;
    }

private:
    alignas(64) std::array<Slot, Capacity> ring;

    alignas(64) uint64_t end_id = 0;
    alignas(64) uint64_t start_id = 0;
};