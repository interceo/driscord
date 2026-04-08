#include <gtest/gtest.h>

#include "utils/chunk_assembler.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

static constexpr size_t kPayload = 100;

// Helper: create a frame of `len` bytes filled with a pattern.
static std::vector<uint8_t> make_frame(size_t len, uint8_t seed = 0) {
    std::vector<uint8_t> v(len);
    std::iota(v.begin(), v.end(), seed);
    return v;
}

// Helper: chunk a frame and collect wire packets.
static std::vector<std::vector<uint8_t>> chunk(
    uint64_t frame_id,
    const std::vector<uint8_t>& frame
) {
    std::vector<std::vector<uint8_t>> packets;
    utils::chunk_frame(
        frame_id,
        frame.data(),
        frame.size(),
        kPayload,
        [&](const uint8_t* data, size_t len) { packets.emplace_back(data, data + len); }
    );
    return packets;
}

// ---- chunk_frame tests ----

TEST(ChunkFrame, SingleChunk) {
    auto frame   = make_frame(50);
    auto packets = chunk(0, frame);

    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(packets[0].size(), protocol::ChunkHeader::kWireSize + 50);

    auto ch = protocol::ChunkHeader::deserialize(packets[0].data());
    EXPECT_EQ(ch.frame_id, 0u);
    EXPECT_EQ(ch.chunk_idx, 0u);
    EXPECT_EQ(ch.total_chunks, 1u);

    // Payload matches original frame
    EXPECT_EQ(
        std::memcmp(packets[0].data() + protocol::ChunkHeader::kWireSize, frame.data(), 50),
        0
    );
}

TEST(ChunkFrame, MultipleChunks) {
    auto frame   = make_frame(250); // 250 / 100 = 3 chunks (100 + 100 + 50)
    auto packets = chunk(7, frame);

    ASSERT_EQ(packets.size(), 3u);

    for (uint16_t i = 0; i < 3; ++i) {
        auto ch = protocol::ChunkHeader::deserialize(packets[i].data());
        EXPECT_EQ(ch.frame_id, 7u);
        EXPECT_EQ(ch.chunk_idx, i);
        EXPECT_EQ(ch.total_chunks, 3u);
    }

    // Last chunk is smaller
    EXPECT_EQ(packets[0].size(), protocol::ChunkHeader::kWireSize + 100);
    EXPECT_EQ(packets[1].size(), protocol::ChunkHeader::kWireSize + 100);
    EXPECT_EQ(packets[2].size(), protocol::ChunkHeader::kWireSize + 50);
}

TEST(ChunkFrame, ExactMultiple) {
    auto frame   = make_frame(200); // 200 / 100 = exactly 2 chunks
    auto packets = chunk(0, frame);

    ASSERT_EQ(packets.size(), 2u);
    EXPECT_EQ(packets[0].size(), protocol::ChunkHeader::kWireSize + 100);
    EXPECT_EQ(packets[1].size(), protocol::ChunkHeader::kWireSize + 100);
}

// ---- ChunkAssembler tests ----

TEST(ChunkAssembler, SingleChunkFrame) {
    utils::ChunkAssembler asm_(kPayload);
    auto frame   = make_frame(50);
    auto packets = chunk(0, frame);

    uint64_t got_id = UINT64_MAX;
    std::vector<uint8_t> got_data;

    bool completed = asm_.push(
        packets[0].data(),
        packets[0].size(),
        [&](uint64_t id, const uint8_t* data, size_t len) {
            got_id = id;
            got_data.assign(data, data + len);
        }
    );

    EXPECT_TRUE(completed);
    EXPECT_EQ(got_id, 0u);
    EXPECT_EQ(got_data, frame);
    EXPECT_EQ(asm_.pending_frames(), 0u);
}

TEST(ChunkAssembler, MultiChunkInOrder) {
    utils::ChunkAssembler asm_(kPayload);
    auto frame   = make_frame(250);
    auto packets = chunk(1, frame);

    std::vector<uint8_t> got_data;
    for (size_t i = 0; i < packets.size(); ++i) {
        bool completed = asm_.push(
            packets[i].data(),
            packets[i].size(),
            [&](uint64_t id, const uint8_t* data, size_t len) {
                EXPECT_EQ(id, 1u);
                got_data.assign(data, data + len);
            }
        );
        EXPECT_EQ(completed, i == packets.size() - 1);
    }

    EXPECT_EQ(got_data, frame);
}

TEST(ChunkAssembler, MultiChunkReverseOrder) {
    utils::ChunkAssembler asm_(kPayload);
    auto frame   = make_frame(250);
    auto packets = chunk(2, frame);

    std::reverse(packets.begin(), packets.end());

    std::vector<uint8_t> got_data;
    for (auto& pkt : packets) {
        asm_.push(pkt.data(), pkt.size(), [&](uint64_t id, const uint8_t* data, size_t len) {
            EXPECT_EQ(id, 2u);
            got_data.assign(data, data + len);
        });
    }

    EXPECT_EQ(got_data, frame);
}

TEST(ChunkAssembler, MultiChunkShuffled) {
    utils::ChunkAssembler asm_(kPayload);
    auto frame   = make_frame(500);
    auto packets = chunk(3, frame);

    std::mt19937 rng(42);
    std::shuffle(packets.begin(), packets.end(), rng);

    std::vector<uint8_t> got_data;
    for (auto& pkt : packets) {
        asm_.push(pkt.data(), pkt.size(), [&](uint64_t id, const uint8_t* data, size_t len) {
            got_data.assign(data, data + len);
        });
    }

    EXPECT_EQ(got_data, frame);
}

TEST(ChunkAssembler, DuplicateChunkIgnored) {
    utils::ChunkAssembler asm_(kPayload);
    auto frame   = make_frame(200);
    auto packets = chunk(0, frame);

    // Push first chunk twice
    asm_.push(packets[0].data(), packets[0].size(), [](uint64_t, const uint8_t*, size_t) {});
    EXPECT_EQ(asm_.pending_frames(), 1u);

    asm_.push(packets[0].data(), packets[0].size(), [](uint64_t, const uint8_t*, size_t) {});
    EXPECT_EQ(asm_.pending_frames(), 1u); // still 1, not completed or duplicated

    std::vector<uint8_t> got_data;
    asm_.push(packets[1].data(), packets[1].size(), [&](uint64_t, const uint8_t* data, size_t len) {
        got_data.assign(data, data + len);
    });
    EXPECT_EQ(got_data, frame);
}

TEST(ChunkAssembler, MultipleFramesInterleaved) {
    utils::ChunkAssembler asm_(kPayload);
    auto frame_a = make_frame(200, 0);
    auto frame_b = make_frame(200, 100);
    auto pkts_a  = chunk(10, frame_a);
    auto pkts_b  = chunk(11, frame_b);

    std::vector<uint8_t> got_a, got_b;
    auto cb = [&](uint64_t id, const uint8_t* data, size_t len) {
        if (id == 10) {
            got_a.assign(data, data + len);
        }
        if (id == 11) {
            got_b.assign(data, data + len);
        }
    };

    // Interleave: A[0], B[0], A[1], B[1]
    asm_.push(pkts_a[0].data(), pkts_a[0].size(), cb);
    asm_.push(pkts_b[0].data(), pkts_b[0].size(), cb);
    EXPECT_EQ(asm_.pending_frames(), 2u);

    asm_.push(pkts_a[1].data(), pkts_a[1].size(), cb);
    EXPECT_EQ(got_a, frame_a);

    asm_.push(pkts_b[1].data(), pkts_b[1].size(), cb);
    EXPECT_EQ(got_b, frame_b);

    EXPECT_EQ(asm_.pending_frames(), 0u);
}

TEST(ChunkAssembler, EvictsOldFrames) {
    utils::ChunkAssembler asm_(kPayload, /*max_frames=*/2);
    auto frame_old = make_frame(200);
    auto pkts_old  = chunk(0, frame_old);

    // Push only first chunk of frame 0 (incomplete)
    asm_.push(pkts_old[0].data(), pkts_old[0].size(), [](uint64_t, const uint8_t*, size_t) {});
    EXPECT_EQ(asm_.pending_frames(), 1u);

    // Push frame 10 — should evict frame 0 (0 + 2 < 10)
    auto frame_new = make_frame(50);
    auto pkts_new  = chunk(10, frame_new);

    std::vector<uint8_t> got;
    asm_.push(
        pkts_new[0].data(),
        pkts_new[0].size(),
        [&](uint64_t, const uint8_t* data, size_t len) { got.assign(data, data + len); }
    );
    EXPECT_EQ(got, frame_new);
    EXPECT_EQ(asm_.pending_frames(), 0u); // old evicted, new completed
}

TEST(ChunkAssembler, RejectsInvalidPackets) {
    utils::ChunkAssembler asm_(kPayload);
    int calls = 0;
    auto cb   = [&](uint64_t, const uint8_t*, size_t) { ++calls; };

    // Too short
    uint8_t tiny[4] = {};
    EXPECT_FALSE(asm_.push(tiny, sizeof(tiny), cb));

    // chunk_idx >= total_chunks
    protocol::ChunkHeader bad{.frame_id = 0, .chunk_idx = 5, .total_chunks = 3};
    uint8_t buf[protocol::ChunkHeader::kWireSize + 10]{};
    bad.serialize(buf);
    EXPECT_FALSE(asm_.push(buf, sizeof(buf), cb));

    // total_chunks = 0
    protocol::ChunkHeader zero{.frame_id = 0, .chunk_idx = 0, .total_chunks = 0};
    zero.serialize(buf);
    EXPECT_FALSE(asm_.push(buf, sizeof(buf), cb));

    EXPECT_EQ(calls, 0);
}

TEST(ChunkAssembler, MismatchedTotalChunksRejected) {
    utils::ChunkAssembler asm_(kPayload);

    // First chunk says total=2
    protocol::ChunkHeader c0{.frame_id = 1, .chunk_idx = 0, .total_chunks = 2};
    std::vector<uint8_t> pkt0(protocol::ChunkHeader::kWireSize + 10);
    c0.serialize(pkt0.data());

    asm_.push(pkt0.data(), pkt0.size(), [](uint64_t, const uint8_t*, size_t) {});

    // Second chunk claims total=3 — mismatch, rejected
    protocol::ChunkHeader c1{.frame_id = 1, .chunk_idx = 1, .total_chunks = 3};
    std::vector<uint8_t> pkt1(protocol::ChunkHeader::kWireSize + 10);
    c1.serialize(pkt1.data());

    bool completed = asm_.push(pkt1.data(), pkt1.size(), [](uint64_t, const uint8_t*, size_t) {});
    EXPECT_FALSE(completed);
}

TEST(ChunkAssembler, Reset) {
    utils::ChunkAssembler asm_(kPayload);
    auto frame   = make_frame(200);
    auto packets = chunk(0, frame);

    asm_.push(packets[0].data(), packets[0].size(), [](uint64_t, const uint8_t*, size_t) {});
    EXPECT_EQ(asm_.pending_frames(), 1u);

    asm_.reset();
    EXPECT_EQ(asm_.pending_frames(), 0u);
}

// ---- Roundtrip: chunk_frame -> ChunkAssembler ----

TEST(ChunkAssembler, FullRoundtrip) {
    utils::ChunkAssembler asm_(kPayload);

    for (uint64_t fid = 0; fid < 10; ++fid) {
        auto frame   = make_frame(73 + fid * 37, static_cast<uint8_t>(fid));
        auto packets = chunk(fid, frame);

        std::vector<uint8_t> got;
        for (auto& pkt : packets) {
            asm_.push(pkt.data(), pkt.size(), [&](uint64_t id, const uint8_t* data, size_t len) {
                EXPECT_EQ(id, fid);
                got.assign(data, data + len);
            });
        }
        EXPECT_EQ(got, frame);
    }
}
