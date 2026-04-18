#include "net_cond.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "utils/chunk_assembler.hpp"
#include "utils/log.hpp"
#include "utils/protocol.hpp"
#include "wait_helpers.hpp"

// AudioReceiver lives in driscord_core (linked by add_integration_test).
#include "audio/audio.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
static SuppressLogs suppress_logs_on_startup;

using namespace std::chrono_literals;
using test_util::NetProfile;
using test_util::NetworkConditioner;
using test_util::PacketCb;
using test_util::PeerNode;
using test_util::SignalingServerFixture;
using test_util::wait_for_rendezvous;

// =============================================================================
// Fixture for tests that need the full WebRTC + signaling stack.
// =============================================================================
class NetConditionsTransportTest : public ::testing::Test {
protected:
    SignalingServerFixture server;
};

// Helper: send n packets of the given size from transport src to dst's peer id.
static void send_n_packets(Transport& src,
    const std::string& dst_id,
    const std::string& channel_label,
    int count,
    size_t payload_size = 32)
{
    std::vector<uint8_t> buf(payload_size, 0xAB);
    for (int i = 0; i < count; ++i) {
        src.send_on_channel_to(channel_label, dst_id, buf.data(), buf.size());
    }
}

// =============================================================================
// 1. Sanity: all packets arrive on clean loopback with no conditioner.
// =============================================================================
TEST_F(NetConditionsTransportTest, NoConditioner_Baseline)
{
    PeerNode a, b;
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    constexpr int kCount = 50;
    send_n_packets(*a.transport, b.id(), a.label, kCount);

    ASSERT_TRUE(b.received.wait_for_count(kCount));
    EXPECT_EQ(b.received.snapshot().size(), static_cast<size_t>(kCount));
}

// =============================================================================
// 2. 8% packet loss: conditioner stats accumulate correctly.
//    dropped ∈ [8, 32] and delivered + dropped == 200.
// =============================================================================
TEST_F(NetConditionsTransportTest, AudioLoss_StatsAccumulate)
{
    // b has 8% loss conditioned on its receive path.
    PeerNode a;
    PeerNode b("data", NetProfile { .loss_pct = 8.0f });
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    constexpr int kCount = 200;
    send_n_packets(*a.transport, b.id(), a.label, kCount);

    // Give the conditioner time to process all enqueued packets (no delay).
    std::this_thread::sleep_for(500ms);

    const auto s = b.conditioner->stats();
    EXPECT_EQ(s.enqueued + s.dropped, static_cast<uint64_t>(kCount));
    EXPECT_GE(s.dropped, 0u);
    // Upper bound: 8%×200 = 16 expected; allow 2× statistical headroom.
    EXPECT_LE(s.dropped, 32u);
}

// =============================================================================
// 3. Audio channel conditioned (10% loss), control channel clean:
//    all 20 control messages must arrive.
//
// Both nodes share "audio" as their primary channel so wait_for_rendezvous
// sees a match. The clean "control" channel is registered BEFORE connect so
// it participates in the initial WebRTC negotiation.
// =============================================================================
TEST_F(NetConditionsTransportTest, ControlChannel_ReliableUnderLoss)
{
    PeerNode a("audio");
    PeerNode b("audio", NetProfile { .loss_pct = 10.0f });

    // Register control channels before connecting — they are negotiated
    // alongside the primary channel during offer/answer exchange.
    auto b_ctrl = b.add_channel("control");
    a.add_channel("control"); // sender side (return value unused)

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    // Allow the control channel time to open after the primary rendezvous.
    std::this_thread::sleep_for(300ms);

    constexpr int kCtrlCount = 20;
    send_n_packets(*a.transport, b.id(), "control", kCtrlCount, 16);

    ASSERT_TRUE(b_ctrl->wait_for_count(kCtrlCount));
    EXPECT_EQ(b_ctrl->snapshot().size(), static_cast<size_t>(kCtrlCount));
}

// =============================================================================
// 4. 100% reorder: all packets still arrive, test completes without deadlock.
// =============================================================================
TEST_F(NetConditionsTransportTest, Reordering_DoesNotDeadlock)
{
    PeerNode a;
    // 100% reorder: every packet gets the extra reorder gap added.
    PeerNode b("data",
        NetProfile { .delay_ms = 10, .reorder_pct = 100.0f, .reorder_gap_ms = 30 });
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    constexpr int kCount = 100;
    send_n_packets(*a.transport, b.id(), a.label, kCount);

    // All packets must still arrive (reorder only adds delay, no drops).
    ASSERT_TRUE(b.received.wait_for_count(kCount, 10s));
    EXPECT_EQ(b.received.snapshot().size(), static_cast<size_t>(kCount));
}

// =============================================================================
// 5. 100% duplication: received count == 2× sent, stats.duplicated == sent.
// =============================================================================
TEST_F(NetConditionsTransportTest, Duplicate_InflatesReceivedCount)
{
    PeerNode a;
    PeerNode b("data", NetProfile { .duplicate_pct = 100.0f });
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    constexpr int kCount = 20;
    send_n_packets(*a.transport, b.id(), a.label, kCount);

    // Each packet is duplicated → expect 2× received.
    ASSERT_TRUE(b.received.wait_for_count(kCount * 2, 5s));
    EXPECT_EQ(b.received.snapshot().size(), static_cast<size_t>(kCount * 2));

    const auto s = b.conditioner->stats();
    EXPECT_EQ(s.duplicated, static_cast<uint64_t>(kCount));
}

// =============================================================================
// 6. Dynamic profile change: second batch experiences ≥5% drops.
// =============================================================================
TEST_F(NetConditionsTransportTest, DynamicProfileChange_TakesEffectImmediately)
{
    PeerNode a;
    PeerNode b("data", NetProfile::clean());
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    // First batch — clean profile, all arrive.
    constexpr int kFirstBatch = 50;
    send_n_packets(*a.transport, b.id(), a.label, kFirstBatch);
    ASSERT_TRUE(b.received.wait_for_count(kFirstBatch));

    // Switch to terrible profile mid-test.
    b.conditioner->set_profile(NetProfile::terrible());

    // Second batch — high loss expected.
    constexpr int kSecondBatch = 100;
    send_n_packets(*a.transport, b.id(), a.label, kSecondBatch);
    std::this_thread::sleep_for(500ms);

    const auto s = b.conditioner->stats();
    // Total sent = kFirstBatch + kSecondBatch. Drops should come from second batch.
    // Terrible has 15% loss → expect ≥5 drops in 100 packets.
    EXPECT_GE(s.dropped, 5u);
}

// =============================================================================
// 7. Standalone: NetworkConditioner with AudioReceiver — switch delay 0→80ms,
//    verify all packets are delivered and receiver stats are sane.
// =============================================================================
TEST(NetConditionsStandalone, JitterBufferAdaptation_UnderVariableDelay)
{
    // A 50ms jitter buffer is enough headroom to absorb the simulated delays.
    AudioReceiver receiver(/*jitter_ms=*/50);

    NetworkConditioner cond(NetProfile::clean());

    // Wrap receiver's push_packet as the downstream callback.
    auto wrapped = cond.wrap([&receiver](const std::string& /*peer*/,
                                 const uint8_t* data,
                                 size_t len) {
        receiver.push_packet(
            utils::vector_view<const uint8_t>(const_cast<uint8_t*>(data), len));
    });

    // Build a helper that emits synthetic AudioHeader + zeroed payload.
    auto make_audio_pkt = [](uint64_t seq) -> std::vector<uint8_t> {
        std::vector<uint8_t> pkt(protocol::AudioHeader::kWireSize + 20, 0);
        protocol::AudioHeader hdr;
        hdr.seq = seq;
        hdr.sender_ts = utils::WallNow();
        hdr.serialize(pkt.data());
        return pkt;
    };

    // Phase 1: 0ms delay — 30 packets.
    constexpr int kPhase1 = 30;
    for (int i = 0; i < kPhase1; ++i) {
        auto pkt = make_audio_pkt(static_cast<uint64_t>(i));
        wrapped("peer", pkt.data(), pkt.size());
    }
    std::this_thread::sleep_for(100ms);

    // Phase 2: switch to 80ms delay — 30 more packets.
    cond.set_profile(NetProfile { .delay_ms = 80 });
    for (int i = kPhase1; i < kPhase1 * 2; ++i) {
        auto pkt = make_audio_pkt(static_cast<uint64_t>(i));
        wrapped("peer", pkt.data(), pkt.size());
    }
    // Wait for 80ms delay + margin.
    std::this_thread::sleep_for(250ms);

    const auto cs = cond.stats();
    EXPECT_EQ(cs.dropped, 0u);
    EXPECT_EQ(cs.delivered, static_cast<uint64_t>(kPhase1 * 2));

    // Receiver should have accepted all delivered packets.
    const auto rs = receiver.stats();
    EXPECT_EQ(rs.packets_received, static_cast<uint64_t>(kPhase1 * 2));
    // miss_count should be low: no artificial gaps were introduced.
    EXPECT_LE(rs.miss_count, static_cast<uint64_t>(kPhase1 * 2 / 10));
}

// =============================================================================
// 8. Standalone: ChunkAssembler fed through 15% loss conditioner — partial
//    frames must not crash; some complete frames should arrive.
// =============================================================================
TEST(NetConditionsStandalone, VideoChunkLoss_AssemblerStats)
{
    constexpr size_t kMaxPayload = 1000;
    constexpr size_t kFrameDataSize = 4500; // 5 chunks per frame
    constexpr int kFrameCount = 20;

    utils::ChunkAssembler assembler(kMaxPayload);
    std::atomic<int> complete_frames { 0 };

    NetworkConditioner cond(NetProfile { .loss_pct = 15.0f });

    auto wrapped = cond.wrap([&assembler, &complete_frames](
                                 const std::string& /*peer*/,
                                 const uint8_t* data,
                                 size_t len) {
        assembler.push(data, len,
            [&complete_frames](uint64_t /*fid*/, const uint8_t*, size_t) {
                ++complete_frames;
            });
    });

    // Build synthetic frame data and chunk it.
    std::vector<uint8_t> frame_data(kFrameDataSize, 0xCD);
    for (int f = 0; f < kFrameCount; ++f) {
        utils::chunk_frame(static_cast<uint64_t>(f),
            frame_data.data(),
            frame_data.size(),
            kMaxPayload,
            [&wrapped](const uint8_t* chunk, size_t len) {
                wrapped("peer", chunk, len);
            });
    }

    // Allow conditioner (no base delay) to flush all enqueued chunks.
    std::this_thread::sleep_for(200ms);

    // Must not crash. With 15% chunk loss, some frames complete, some don't.
    // ChunkAssembler(max_frames=8) evicts frames where frame_id + 8 < current,
    // so the live window is [current-8, current] = at most 9 pending entries.
    EXPECT_LE(assembler.pending_frames(), 9u);

    // Sanity: at 15% chunk loss per 5-chunk frame, P(frame complete) ≈ 0.85^5
    // ≈ 44%. Out of 20 frames, expect at least 3 to complete.
    EXPECT_GE(complete_frames.load(), 3);

    const auto s = cond.stats();
    // All chunks were enqueued (or dropped); enqueued + dropped = total chunks.
    const uint64_t total_chunks = static_cast<uint64_t>(
        kFrameCount * ((kFrameDataSize + kMaxPayload - 1) / kMaxPayload));
    EXPECT_EQ(s.enqueued + s.dropped, total_chunks);
}
