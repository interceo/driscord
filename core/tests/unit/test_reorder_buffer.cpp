#include <gtest/gtest.h>

#include "utils/reorder_buffer.hpp"

#include <algorithm>
#include <random>
#include <vector>

using utils::PushResult;
using utils::ReorderBuffer;

namespace {

struct Payload {
    int value = 0;
    bool operator==(const Payload&) const = default;
};

using Buf = ReorderBuffer<Payload, 64>;

Payload p(int v) { return Payload { v }; }

} // namespace

TEST(ReorderBuffer, StartsEmpty)
{
    Buf b;
    EXPECT_TRUE(b.empty());
    EXPECT_FALSE(b.started());
    EXPECT_EQ(b.size(), 0u);
    EXPECT_FALSE(b.next_present().has_value());
    EXPECT_EQ(b.peek(0), nullptr);
}

TEST(ReorderBuffer, FirstPushDefinesBase)
{
    Buf b;
    EXPECT_EQ(b.push(1000, p(1)), PushResult::Stored);
    EXPECT_EQ(b.base_seq(), 1000u);
    EXPECT_EQ(b.size(), 1u);
    ASSERT_EQ(b.next_present(), std::optional<uint64_t>(1000));
}

TEST(ReorderBuffer, InOrderTakeAdvances)
{
    Buf b;
    for (int i = 0; i < 10; ++i) {
        ASSERT_EQ(b.push(static_cast<uint64_t>(i), p(i)), PushResult::Stored);
    }
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(b.contains(static_cast<uint64_t>(i)));
        EXPECT_EQ(b.take(static_cast<uint64_t>(i)).value, i);
        b.advance_base_to(static_cast<uint64_t>(i) + 1);
    }
    EXPECT_TRUE(b.empty());
    EXPECT_EQ(b.base_seq(), 10u);
}

// The cursor starts wherever the first packet happens to land. Until something
// is actually read, an earlier arrival must pull it back rather than be thrown
// away — otherwise reordering across the very first packets loses audio.
TEST(ReorderBuffer, StartOfStreamReorderRewindsCursor)
{
    Buf b;
    for (uint64_t seq : { 3u, 1u, 4u, 0u, 2u }) {
        ASSERT_EQ(b.push(seq, p(static_cast<int>(seq))), PushResult::Stored) << seq;
    }
    EXPECT_EQ(b.base_seq(), 0u);
    EXPECT_EQ(b.size(), 5u);

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(b.contains(static_cast<uint64_t>(i)));
        EXPECT_EQ(b.take(static_cast<uint64_t>(i)).value, i);
        b.advance_base_to(static_cast<uint64_t>(i) + 1);
    }
}

// Once playback has started the cursor may only move forward: rewinding would
// mean re-playing audio the listener already heard.
TEST(ReorderBuffer, CursorDoesNotRewindAfterFirstRead)
{
    Buf b;
    b.push(5, p(5));
    b.take(5);
    EXPECT_EQ(b.push(4, p(4)), PushResult::TooLate);
    EXPECT_EQ(b.base_seq(), 5u);
}

TEST(ReorderBuffer, LatePacketRejected)
{
    Buf b;
    b.push(10, p(10));
    b.advance_base_to(15);
    EXPECT_EQ(b.push(12, p(12)), PushResult::TooLate);
    EXPECT_FALSE(b.contains(12));
}

TEST(ReorderBuffer, DuplicateRejected)
{
    Buf b;
    ASSERT_EQ(b.push(5, p(5)), PushResult::Stored);
    EXPECT_EQ(b.push(5, p(99)), PushResult::Duplicate);
    EXPECT_EQ(b.size(), 1u);
    ASSERT_NE(b.peek(5), nullptr);
    EXPECT_EQ(b.peek(5)->value, 5); // original kept, not overwritten
}

TEST(ReorderBuffer, GapIsVisibleAndSkippable)
{
    Buf b;
    b.push(0, p(0));
    b.push(3, p(3));

    EXPECT_EQ(b.take(0).value, 0);
    b.advance_base_to(1);

    // 1 and 2 never arrived; the next thing to play is 3.
    EXPECT_FALSE(b.contains(1));
    EXPECT_EQ(b.next_present(), std::optional<uint64_t>(3));

    b.advance_base_to(3);
    EXPECT_EQ(b.take(3).value, 3);
    EXPECT_TRUE(b.empty());
}

TEST(ReorderBuffer, NextPresentSkipsToLaterPacket)
{
    Buf b;
    b.push(0, p(0));
    b.push(40, p(40));
    b.advance_base_to(1);
    EXPECT_EQ(b.next_present(), std::optional<uint64_t>(40));
    EXPECT_EQ(b.next_present(41), std::nullopt);
}

// The occupancy bitmask is scanned word by word; a packet sitting exactly on
// a 64-bit boundary is where an off-by-one in that scan would show up.
TEST(ReorderBuffer, NextPresentAcrossWordBoundaries)
{
    ReorderBuffer<Payload, 256> b;
    b.push(0, p(0));
    b.advance_base_to(1);

    for (uint64_t target : { 63u, 64u, 65u, 127u, 128u, 191u, 192u, 255u }) {
        ReorderBuffer<Payload, 256> c;
        c.push(0, p(0));
        c.take(0);
        c.advance_base_to(1);
        ASSERT_EQ(c.push(target, p(static_cast<int>(target))), PushResult::Stored)
            << "target=" << target;
        EXPECT_EQ(c.next_present(), std::optional<uint64_t>(target))
            << "target=" << target;
    }
}

TEST(ReorderBuffer, NextPresentHonoursFromArgument)
{
    Buf b;
    b.push(0, p(0));
    b.push(5, p(5));
    b.push(9, p(9));

    EXPECT_EQ(b.next_present(0), std::optional<uint64_t>(0));
    EXPECT_EQ(b.next_present(1), std::optional<uint64_t>(5));
    EXPECT_EQ(b.next_present(6), std::optional<uint64_t>(9));
    EXPECT_EQ(b.next_present(10), std::nullopt);
    // Below the cursor clamps to the cursor rather than reading stale slots.
    EXPECT_EQ(b.next_present(0), b.next_present(b.base_seq()));
}

TEST(ReorderBuffer, TooFarAheadResyncs)
{
    Buf b;
    b.push(0, p(0));
    b.push(1, p(1));
    ASSERT_EQ(b.size(), 2u);

    // A jump past the window (e.g. the sender was silent for a minute).
    EXPECT_EQ(b.push(Buf::kCapacity + 100, p(7)), PushResult::Resynced);
    EXPECT_EQ(b.base_seq(), Buf::kCapacity + 100);
    EXPECT_EQ(b.size(), 1u);
    EXPECT_FALSE(b.contains(0));
    ASSERT_NE(b.peek(Buf::kCapacity + 100), nullptr);
    EXPECT_EQ(b.peek(Buf::kCapacity + 100)->value, 7);
}

TEST(ReorderBuffer, FillsToCapacityAndWraps)
{
    Buf b;
    for (uint64_t i = 0; i < Buf::kCapacity; ++i) {
        ASSERT_EQ(b.push(i, p(static_cast<int>(i))), PushResult::Stored) << i;
    }
    EXPECT_EQ(b.size(), Buf::kCapacity);

    // One past the window resyncs rather than aliasing onto slot 0.
    EXPECT_EQ(b.push(Buf::kCapacity, p(1234)), PushResult::Resynced);
    EXPECT_EQ(b.size(), 1u);

    // Draining and refilling reuses the same slots correctly.
    b.take(Buf::kCapacity);
    b.advance_base_to(Buf::kCapacity + 1);
    for (uint64_t i = Buf::kCapacity + 1; i < 2 * Buf::kCapacity; ++i) {
        ASSERT_EQ(b.push(i, p(static_cast<int>(i))), PushResult::Stored) << i;
    }
    EXPECT_EQ(b.size(), Buf::kCapacity - 1);
}

TEST(ReorderBuffer, AdvanceBaseDropsHeldPackets)
{
    Buf b;
    for (uint64_t i = 0; i < 10; ++i) {
        b.push(i, p(static_cast<int>(i)));
    }
    b.advance_base_to(7);
    EXPECT_EQ(b.base_seq(), 7u);
    EXPECT_EQ(b.size(), 3u);
    EXPECT_FALSE(b.contains(6));
    EXPECT_TRUE(b.contains(7));
}

TEST(ReorderBuffer, AdvanceBaseBeyondWindowClearsEverything)
{
    Buf b;
    for (uint64_t i = 0; i < 10; ++i) {
        b.push(i, p(static_cast<int>(i)));
    }
    b.advance_base_to(10'000);
    EXPECT_EQ(b.base_seq(), 10'000u);
    EXPECT_TRUE(b.empty());
    EXPECT_EQ(b.next_present(), std::nullopt);
}

TEST(ReorderBuffer, AdvanceBaseIsMonotonic)
{
    Buf b;
    b.push(5, p(5));
    b.advance_base_to(8);
    b.advance_base_to(3); // ignored
    EXPECT_EQ(b.base_seq(), 8u);
}

TEST(ReorderBuffer, ResetClearsEverything)
{
    Buf b;
    for (uint64_t i = 0; i < 10; ++i) {
        b.push(100 + i, p(static_cast<int>(i)));
    }
    b.reset();
    EXPECT_TRUE(b.empty());
    EXPECT_FALSE(b.started());
    EXPECT_EQ(b.next_present(), std::nullopt);

    // Reusable afterwards, with a fresh base.
    EXPECT_EQ(b.push(42, p(1)), PushResult::Stored);
    EXPECT_EQ(b.base_seq(), 42u);
}

// Payload memory must not stay pinned in slots the cursor has moved past,
// otherwise a video buffer would hold a full window of decoded frames alive.
TEST(ReorderBuffer, DroppedSlotsReleasePayload)
{
    ReorderBuffer<std::vector<int>, 64> b;
    b.push(0, std::vector<int>(1000, 7));
    b.push(1, std::vector<int>(1000, 7));
    ASSERT_NE(b.peek(0), nullptr);
    ASSERT_EQ(b.peek(0)->size(), 1000u);

    b.advance_base_to(2);
    EXPECT_TRUE(b.empty());

    // Re-push into the same slots; nothing from the old contents survives.
    b.push(64, std::vector<int> { 1, 2, 3 });
    ASSERT_NE(b.peek(64), nullptr);
    EXPECT_EQ(b.peek(64)->size(), 3u);
}

TEST(ReorderBuffer, ResetReleasesPayload)
{
    ReorderBuffer<std::vector<int>, 64> b;
    b.push(0, std::vector<int>(1000, 7));
    b.reset();
    b.push(0, std::vector<int> { 9 });
    ASSERT_NE(b.peek(0), nullptr);
    EXPECT_EQ(b.peek(0)->size(), 1u);
}

TEST(ReorderBuffer, TakeLeavesSlotFree)
{
    Buf b;
    b.push(3, p(3));
    EXPECT_EQ(b.take(3).value, 3);
    EXPECT_FALSE(b.contains(3));
    EXPECT_EQ(b.peek(3), nullptr);
    EXPECT_TRUE(b.empty());
    // The slot accepts a later sequence number that maps to it.
    EXPECT_EQ(b.push(3 + Buf::kCapacity - 1, p(1)), PushResult::Stored);
}

// Shuffled delivery with losses and duplicates: every packet that was stored
// must come back exactly once, in ascending order, and nothing else may.
TEST(ReorderBuffer, StressShuffledDelivery)
{
    std::mt19937 rng(1234);
    ReorderBuffer<Payload, 128> b;

    uint64_t next_send = 0;
    uint64_t cursor = 0;
    std::vector<uint64_t> in_flight;
    std::vector<int> played;
    std::vector<int> stored;

    for (int round = 0; round < 500; ++round) {
        for (int i = 0; i < 8; ++i) {
            in_flight.push_back(next_send++);
        }
        std::shuffle(in_flight.begin(), in_flight.end(), rng);

        // Deliver most of them; drop a few, duplicate a few.
        std::vector<uint64_t> keep;
        for (uint64_t seq : in_flight) {
            if (rng() % 20 == 0) { // loss
                continue;
            }
            if (rng() % 8 == 0) { // hold back for a later round (reorder)
                keep.push_back(seq);
                continue;
            }
            const auto r = b.push(seq, p(static_cast<int>(seq)));
            if (r == PushResult::Stored) {
                stored.push_back(static_cast<int>(seq));
            }
            if (rng() % 10 == 0) {
                EXPECT_EQ(b.push(seq, p(static_cast<int>(seq))),
                    PushResult::Duplicate);
            }
        }
        in_flight = std::move(keep);

        // Drain everything that is playable, concealing gaps.
        while (cursor + 16 < next_send) {
            if (b.contains(cursor)) {
                played.push_back(b.take(cursor).value);
            }
            ++cursor;
            b.advance_base_to(cursor);
        }
    }

    EXPECT_TRUE(std::is_sorted(played.begin(), played.end()));
    EXPECT_EQ(std::adjacent_find(played.begin(), played.end()), played.end())
        << "a packet was played twice";
    for (int v : played) {
        EXPECT_NE(std::find(stored.begin(), stored.end(), v), stored.end())
            << "played a packet that was never stored: " << v;
    }
    EXPECT_GT(played.size(), 3000u) << "test delivered too little to be meaningful";
}
