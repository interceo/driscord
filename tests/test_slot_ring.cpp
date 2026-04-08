#include <gtest/gtest.h>

#include "utils/slot_ring.hpp"

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

struct Payload {
    int value = -1;
};

using Ring = SlotRing<Payload, 16>;

// 1. Basic push/pop
TEST(SlotRing, BasicPushPop) {
    Ring ring;
    ASSERT_TRUE(ring.push(0, Payload{10}));
    ASSERT_TRUE(ring.push(1, Payload{20}));
    ASSERT_TRUE(ring.push(2, Payload{30}));
    EXPECT_EQ(ring.size(), 3u);

    auto r0 = ring.pop();
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(r0->data.value, 10);
    EXPECT_EQ(r0->skipped, 0u);

    auto r1 = ring.pop();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->data.value, 20);

    auto r2 = ring.pop();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->data.value, 30);

    EXPECT_TRUE(ring.empty());
}

// 2. Out-of-order push — future seqs accepted, past seqs rejected
TEST(SlotRing, OutOfOrderPush) {
    Ring ring;
    // First push initializes next_seq_ = 0
    ASSERT_TRUE(ring.push(0, Payload{10}));
    // Push seq 2 before seq 1 — both are >= next_seq_, so accepted
    ASSERT_TRUE(ring.push(2, Payload{30}));
    ASSERT_TRUE(ring.push(1, Payload{20}));

    // Pop returns in seq order: 0, 1, 2
    auto r0 = ring.pop();
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(r0->data.value, 10);

    auto r1 = ring.pop();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->data.value, 20);

    auto r2 = ring.pop();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->data.value, 30);
}

// 2b. Before any pop, seqs below next_seq_ are accepted (out-of-order arrival)
TEST(SlotRing, OutOfOrderBeforePop) {
    Ring ring;
    // First push with seq=5 sets next_seq_ = 5
    ASSERT_TRUE(ring.push(5, Payload{50}));
    // seq=3 < next_seq_=5, but no pop yet — accepted, next_seq_ adjusts to 3
    ASSERT_TRUE(ring.push(3, Payload{30}));
    // seq=4, now >= next_seq_=3, accepted
    ASSERT_TRUE(ring.push(4, Payload{40}));
    // seq=6 >= next_seq_=3, accepted
    ASSERT_TRUE(ring.push(6, Payload{60}));
    EXPECT_EQ(ring.size(), 4u);

    // Pop returns in seq order: 3, 4, 5, 6
    auto r0 = ring.pop();
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(r0->data.value, 30);

    auto r1 = ring.pop();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->data.value, 40);

    auto r2 = ring.pop();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->data.value, 50);

    auto r3 = ring.pop();
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(r3->data.value, 60);
}

// 2c. After pop, seqs below next_seq_ are rejected
TEST(SlotRing, OutOfOrderRejectedAfterPop) {
    Ring ring;
    ASSERT_TRUE(ring.push(5, Payload{50}));
    ring.pop(); // next_seq_ becomes 6, popped_ = true
    EXPECT_FALSE(ring.push(3, Payload{30}));
    ASSERT_TRUE(ring.push(6, Payload{60}));
}

// 3. Duplicate seq rejected
TEST(SlotRing, DuplicateSeqRejected) {
    Ring ring;
    ASSERT_TRUE(ring.push(5, Payload{50}));
    EXPECT_FALSE(ring.push(5, Payload{99}));
    EXPECT_EQ(ring.size(), 1u);

    auto r = ring.pop();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->data.value, 50);
}

// 4. Old seq rejected after pop advances next_seq_
TEST(SlotRing, OldSeqRejectedAfterPop) {
    Ring ring;
    ASSERT_TRUE(ring.push(5, Payload{50}));
    ring.pop(); // next_seq_ becomes 6
    EXPECT_FALSE(ring.push(3, Payload{30}));
    EXPECT_FALSE(ring.push(5, Payload{50}));
    ASSERT_TRUE(ring.push(6, Payload{60}));
}

// 5. Gap detection via peek
TEST(SlotRing, GapDetection) {
    Ring ring;
    ASSERT_TRUE(ring.push(0, Payload{10}));
    ASSERT_TRUE(ring.push(2, Payload{30}));

    auto p0 = ring.peek_next();
    ASSERT_TRUE(p0.has_value());
    EXPECT_EQ(p0->seq, 0u);
    EXPECT_EQ(p0->skipped, 0u);

    ring.consume_peeked(0);

    auto p1 = ring.peek_next();
    ASSERT_TRUE(p1.has_value());
    EXPECT_EQ(p1->seq, 2u);
    EXPECT_EQ(p1->skipped, 1u);
}

// 6. Wraparound — push Capacity+1 elements
TEST(SlotRing, Wraparound) {
    Ring ring;
    for (uint64_t i = 0; i < 17; ++i) {
        ring.push(i, Payload{static_cast<int>(i)});
    }

    // First element (seq=0) was overwritten by seq=16 (same slot).
    // next_seq_ is 0, but slot 0 now has seq=16, so peek skips ahead.
    auto p = ring.peek_next();
    ASSERT_TRUE(p.has_value());
    // Seq 0 was overwritten, so the first available is seq 1
    EXPECT_EQ(p->seq, 1u);
    EXPECT_EQ(p->skipped, 1u);
}

// 7. Reset
TEST(SlotRing, Reset) {
    Ring ring;
    ring.push(0, Payload{10});
    ring.push(1, Payload{20});
    ring.reset();

    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.size(), 0u);
    EXPECT_FALSE(ring.initialized());

    // After reset, can push starting from any seq
    ASSERT_TRUE(ring.push(100, Payload{100}));
    auto r = ring.pop();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->data.value, 100);
}

// 8. for_each_occupied
TEST(SlotRing, ForEachOccupied) {
    Ring ring;
    ring.push(0, Payload{10});
    ring.push(3, Payload{40});
    ring.push(7, Payload{80});

    std::vector<int> values;
    ring.for_each_occupied([&](const Ring::Slot& s) { values.push_back(s.data.value); });

    EXPECT_EQ(values.size(), 3u);
    std::sort(values.begin(), values.end());
    EXPECT_EQ(values, (std::vector<int>{10, 40, 80}));
}

// 9. Capacity overflow — push more than Capacity with sequential seqs
TEST(SlotRing, CapacityOverflow) {
    Ring ring;
    for (uint64_t i = 0; i < 32; ++i) {
        ring.push(i, Payload{static_cast<int>(i)});
    }

    // Size capped at Capacity (16) since each new push overwrites old slot
    EXPECT_LE(ring.size(), 16u);

    // After overflow without any pops, next_seq_ is still 0 but those slots
    // were overwritten (now hold seqs 16-31), so peek_next can't find seq 0-15.
    // This is expected — the ring is designed for consumers that keep up.
    // Verify the ring is still consistent: all 16 occupied slots hold seqs 16-31.
    std::vector<int> values;
    ring.for_each_occupied([&](const Ring::Slot& s) { values.push_back(s.data.value); });
    std::sort(values.begin(), values.end());
    ASSERT_EQ(values.size(), 16u);
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(values[i], 16 + i);
    }
}

// 10. Stress random
TEST(SlotRing, StressRandom) {
    SlotRing<Payload, 64> ring;
    std::mt19937 rng(42);

    // Generate random sequence numbers in range [0, 200)
    std::vector<uint64_t> seqs(200);
    std::iota(seqs.begin(), seqs.end(), 0);
    std::shuffle(seqs.begin(), seqs.end(), rng);

    for (auto seq : seqs) {
        ring.push(seq, Payload{static_cast<int>(seq)});
        EXPECT_LE(ring.size(), 64u);
    }

    // Pop all — should come out in order
    uint64_t last_seq = 0;
    bool first        = true;
    while (auto r = ring.pop()) {
        if (!first) {
            EXPECT_GT(r->data.value, static_cast<int>(last_seq));
        }
        last_seq = r->data.value;
        first    = false;
    }
}

// advance_seq
TEST(SlotRing, AdvanceSeq) {
    Ring ring;
    ring.push(0, Payload{10});
    ring.push(1, Payload{20});

    EXPECT_EQ(ring.next_seq(), 0u);
    ring.advance_seq();
    EXPECT_EQ(ring.next_seq(), 1u);

    auto r = ring.pop();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->data.value, 20);
}

// Empty pop returns nullopt
TEST(SlotRing, EmptyPopReturnsNullopt) {
    Ring ring;
    EXPECT_FALSE(ring.pop().has_value());
}
