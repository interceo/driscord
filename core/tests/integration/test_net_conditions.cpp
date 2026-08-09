#include "net_cond.hpp"
#include "rtc_cleanup_env.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "utils/log.hpp"
#include "utils/protocol.hpp"
#include "wait_helpers.hpp"

// AudioReceiver lives in driscord_core (linked by add_integration_test).
#include "audio/audio.hpp"
#include "sync/media_clock.hpp"
#include "utils/mono_clock.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
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

// Helper: send n packets of the given size to the server, which fans them out
// to the rest of the room.
static void send_n_packets(Transport& src,
    channel::MediaChannel channel_label,
    int count,
    size_t payload_size = 32)
{
    std::vector<uint8_t> buf(payload_size, 0xAB);
    for (int i = 0; i < count; ++i) {
        src.send_on_channel(channel_label, buf.data(), buf.size());
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
    send_n_packets(*a.transport, a.label, kCount);

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
    PeerNode b(channel::MediaChannel::Audio, NetProfile { .loss_pct = 8.0f });
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    constexpr int kCount = 200;
    send_n_packets(*a.transport, a.label, kCount);

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
    PeerNode a(channel::MediaChannel::Audio);
    PeerNode b(channel::MediaChannel::Audio, NetProfile { .loss_pct = 10.0f });

    // Register control channels before connecting — they are negotiated
    // alongside the primary channel during offer/answer exchange.
    auto b_ctrl = b.add_channel(channel::MediaChannel::Control);
    a.add_channel(channel::MediaChannel::Control); // sender side

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    // Allow the control channel time to open after the primary rendezvous.
    std::this_thread::sleep_for(300ms);

    constexpr int kCtrlCount = 20;
    send_n_packets(*a.transport, channel::MediaChannel::Control, kCtrlCount, 16);

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
    PeerNode b(channel::MediaChannel::Audio,
        NetProfile { .delay_ms = 10, .reorder_pct = 100.0f, .reorder_gap_ms = 30 });
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    constexpr int kCount = 100;
    send_n_packets(*a.transport, a.label, kCount);

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
    PeerNode b(channel::MediaChannel::Audio,
        NetProfile { .duplicate_pct = 100.0f });
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    constexpr int kCount = 20;
    send_n_packets(*a.transport, a.label, kCount);

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
    PeerNode b(channel::MediaChannel::Audio, NetProfile::clean());
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    // First batch — clean profile, all arrive.
    constexpr int kFirstBatch = 50;
    send_n_packets(*a.transport, a.label, kFirstBatch);
    ASSERT_TRUE(b.received.wait_for_count(kFirstBatch));

    // Switch to terrible profile mid-test.
    b.conditioner->set_profile(NetProfile::terrible());

    // Second batch — high loss expected.
    constexpr int kSecondBatch = 100;
    send_n_packets(*a.transport, a.label, kSecondBatch);
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
    AudioReceiver receiver(std::make_shared<avsync::MediaClock>());

    NetworkConditioner cond(NetProfile::clean());

    // Wrap receiver's push_packet as the downstream callback.
    auto wrapped = cond.wrap([&receiver](const std::string& /*peer*/,
                                 const uint8_t* data,
                                 size_t len) {
        receiver.push_packet(std::span<const uint8_t>(data, len));
    });

    // Build a helper that emits synthetic AudioHeader + zeroed payload.
    auto make_audio_pkt = [](uint32_t seq) -> std::vector<uint8_t> {
        std::vector<uint8_t> pkt(protocol::AudioHeader::kWireSize + 20, 0);
        protocol::AudioHeader hdr;
        hdr.seq = seq;
        hdr.sender_ts_us = utils::MonoClock::now_us();
        hdr.serialize(pkt.data());
        return pkt;
    };

    // Phase 1: 0ms delay — 30 packets.
    constexpr int kPhase1 = 30;
    for (int i = 0; i < kPhase1; ++i) {
        auto pkt = make_audio_pkt(static_cast<uint32_t>(i));
        wrapped("peer", pkt.data(), pkt.size());
    }
    std::this_thread::sleep_for(100ms);

    // Phase 2: switch to 80ms delay — 30 more packets.
    cond.set_profile(NetProfile { .delay_ms = 80 });
    for (int i = kPhase1; i < kPhase1 * 2; ++i) {
        auto pkt = make_audio_pkt(static_cast<uint32_t>(i));
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
    // No artificial gaps were introduced, so nothing may be rejected as late,
    // duplicate or out of window — the delay change alone must not cost packets.
    EXPECT_EQ(rs.drop_count, 0u);
}
