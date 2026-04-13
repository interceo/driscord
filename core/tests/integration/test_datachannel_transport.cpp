#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "utils/log.hpp"
#include "wait_helpers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
static SuppressLogs suppress_logs_on_startup;

using namespace std::chrono_literals;
using test_util::kDefaultTimeout;
using test_util::make_test_transport;
using test_util::PeerNode;
using test_util::ReceivedPacket;
using test_util::SignalingServerFixture;
using test_util::wait_for_local_id;
using test_util::wait_for_rendezvous;

// =============================================================================
// Test fixture: each test gets a fresh in-process signaling server on an
// ephemeral port. The fixture is declared as a member so it is constructed
// BEFORE test-body locals and destroyed AFTER them — RAII order ensures
// Transports tear down against a still-alive server.
// =============================================================================
class DatachannelTransportTest : public ::testing::Test {
protected:
    SignalingServerFixture server;
};

// =============================================================================
// P0 — Critical tests
// =============================================================================

// 1. Two transports rendezvous via signaling and see each other.
TEST_F(DatachannelTransportTest, Rendezvous_TwoPeers)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));

    ASSERT_FALSE(a.id().empty());
    ASSERT_FALSE(b.id().empty());
    EXPECT_NE(a.id(), b.id());

    ASSERT_TRUE(a.joined.wait_for_count(1));
    ASSERT_TRUE(b.joined.wait_for_count(1));

    EXPECT_EQ(a.joined.snapshot().front(), b.id());
    EXPECT_EQ(b.joined.snapshot().front(), a.id());

    ASSERT_EQ(a.transport->peers().size(), 1u);
    ASSERT_EQ(b.transport->peers().size(), 1u);
    EXPECT_EQ(a.transport->peers().front().id, b.id());
    EXPECT_EQ(b.transport->peers().front().id, a.id());
}

// 2. DataChannel open callback fires on both sides.
TEST_F(DatachannelTransportTest, DataChannel_OpensOnBothEnds)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    EXPECT_EQ(a.channel_open_events.snapshot().front(), b.id());
    EXPECT_EQ(b.channel_open_events.snapshot().front(), a.id());
}

// 3. send_on_channel_to delivers a byte-exact payload to exactly one peer.
TEST_F(DatachannelTransportTest, SendOnChannelTo_PointToPoint)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> payload {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04
    };
    a.transport->send_on_channel_to(
        "data", b.id(), payload.data(), payload.size());

    ASSERT_TRUE(b.received.wait_for_count(1));
    auto items = b.received.snapshot();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front().peer, a.id());
    EXPECT_EQ(items.front().bytes, payload);
}

// 4. peer_left fires on the remaining peer after the other disconnects.
TEST_F(DatachannelTransportTest, PeerLeft_FiresOnRemainingPeer)
{
    PeerNode a;
    auto b = std::make_unique<PeerNode>();

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b->connect(server.ws_url()));

    ASSERT_TRUE(a.joined.wait_for_count(1));
    const std::string b_id = b->id();
    EXPECT_EQ(a.joined.snapshot().front(), b_id);

    // Tear down b → server emits peer_left to a.
    b.reset();

    ASSERT_TRUE(a.left.wait_for_count(1));
    EXPECT_EQ(a.left.snapshot().front(), b_id);
    EXPECT_EQ(a.transport->peers().size(), 0u);
}

// 5. Repeated connect/disconnect must not leak or deadlock. Protected by the
// CTest TIMEOUT so a hung teardown fails the test instead of CI.
TEST(DatachannelTransport, RepeatedConnectDisconnect_NoLeaksOrHangs)
{
    constexpr int kIterations = 5;

    for (int i = 0; i < kIterations; ++i) {
        SignalingServerFixture server;

        auto a = std::make_unique<PeerNode>();
        auto b = std::make_unique<PeerNode>();

        ASSERT_TRUE(a->connect(server.ws_url())) << "iteration " << i;
        ASSERT_TRUE(b->connect(server.ws_url())) << "iteration " << i;
        ASSERT_TRUE(wait_for_rendezvous(*a, *b)) << "iteration " << i;

        a.reset();
        b.reset();
    }
}

// =============================================================================
// P1 — Important tests
// =============================================================================

// 6. send_on_channel broadcasts to all connected peers.
TEST_F(DatachannelTransportTest, SendOnChannel_BroadcastToAllPeers)
{
    PeerNode a, b, c;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(c.connect(server.ws_url()));

    // A must see both B and C, and its "data" channel must be open to
    // both of them before we broadcast.
    ASSERT_TRUE(a.joined.wait_for_count(2));
    ASSERT_TRUE(a.channel_open_events.wait_for_count(2));
    ASSERT_TRUE(b.channel_open.wait_for());
    ASSERT_TRUE(c.channel_open.wait_for());

    const std::vector<uint8_t> payload { 0xCA, 0xFE, 0xBA, 0xBE };
    a.transport->send_on_channel("data", payload.data(), payload.size());

    ASSERT_TRUE(b.received.wait_for_count(1));
    ASSERT_TRUE(c.received.wait_for_count(1));

    EXPECT_EQ(b.received.snapshot().front().bytes, payload);
    EXPECT_EQ(c.received.snapshot().front().bytes, payload);
    EXPECT_EQ(b.received.snapshot().front().peer, a.id());
    EXPECT_EQ(c.received.snapshot().front().peer, a.id());

    // Broadcasting from A must not deliver back to A (the sender is not
    // looped through its own on_data).
    EXPECT_EQ(a.received.size(), 0u);
}

// 7. Multiple channels per peer — "data" vs "video" labels are routed
//    independently.
TEST_F(DatachannelTransportTest, MultipleChannels_RoutedIndependently)
{
    PeerNode a("data");
    PeerNode b("data");
    auto a_video = a.add_channel("video");
    auto b_video = b.add_channel("video");

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> data_payload { 0x01, 0x02, 0x03 };
    const std::vector<uint8_t> video_payload { 0xFF, 0xEE, 0xDD, 0xCC };

    a.transport->send_on_channel_to(
        "data", b.id(), data_payload.data(), data_payload.size());
    a.transport->send_on_channel_to(
        "video", b.id(), video_payload.data(), video_payload.size());

    ASSERT_TRUE(b.received.wait_for_count(1));
    ASSERT_TRUE(b_video->wait_for_count(1));

    // Payloads must land on the correct channel's collector.
    EXPECT_EQ(b.received.snapshot().front().bytes, data_payload);
    EXPECT_EQ(b_video->snapshot().front().bytes, video_payload);

    // Cross-contamination check: after waiting a short extra grace period,
    // neither collector should grow beyond 1.
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(b.received.size(), 1u);
    EXPECT_EQ(b_video->size(), 1u);
    EXPECT_EQ(a.received.size(), 0u);
    EXPECT_EQ(a_video->size(), 0u);
}

// 8. Move-based send overload (rtc::binary&&) delivers the payload intact.
TEST_F(DatachannelTransportTest, MoveOverload_SendOnChannelTo)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> expected {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };
    rtc::binary payload;
    payload.reserve(expected.size());
    for (uint8_t byte : expected) {
        payload.push_back(std::byte { byte });
    }
    a.transport->send_on_channel_to("data", b.id(), std::move(payload));

    ASSERT_TRUE(b.received.wait_for_count(1));
    EXPECT_EQ(b.received.snapshot().front().bytes, expected);
}

// 9. send_streaming_start / _stop notify all other peers via the signaling
//    server, firing the corresponding on_streaming_* callbacks.
TEST_F(DatachannelTransportTest, StreamingSignals_PropagateToOtherPeers)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));

    // Wait for A to see B before sending control signals — the server only
    // broadcasts to already-registered sessions.
    ASSERT_TRUE(a.joined.wait_for_count(1));
    ASSERT_TRUE(b.joined.wait_for_count(1));

    a.transport->send_streaming_start();
    ASSERT_TRUE(b.streaming_started.wait_for_count(1));
    EXPECT_EQ(b.streaming_started.snapshot().front(), a.id());

    a.transport->send_streaming_stop();
    ASSERT_TRUE(b.streaming_stopped.wait_for_count(1));
    EXPECT_EQ(b.streaming_stopped.snapshot().front(), a.id());

    // The sender's own streaming callbacks should not fire.
    EXPECT_EQ(a.streaming_started.size(), 0u);
    EXPECT_EQ(a.streaming_stopped.size(), 0u);
}

// 10. send_watch_start / _stop propagate via the signaling server.
TEST_F(DatachannelTransportTest, WatchSignals_PropagateToOtherPeers)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(a.joined.wait_for_count(1));
    ASSERT_TRUE(b.joined.wait_for_count(1));

    a.transport->send_watch_start();
    ASSERT_TRUE(b.watch_started.wait_for_count(1));
    EXPECT_EQ(b.watch_started.snapshot().front(), a.id());

    a.transport->send_watch_stop();
    ASSERT_TRUE(b.watch_stopped.wait_for_count(1));
    EXPECT_EQ(b.watch_stopped.snapshot().front(), a.id());
}

// 11. Large (~60 KB) payload through a DataChannel. Transport is configured
//     with rtc_config_.maxMessageSize = 128 KB, so 60 KB fits a single SCTP
//     message without application-level chunking.
TEST_F(DatachannelTransportTest, LargePayload_60KBDelivered)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    constexpr size_t kSize = 60 * 1024;
    std::vector<uint8_t> payload(kSize);
    for (size_t i = 0; i < kSize; ++i) {
        payload[i] = static_cast<uint8_t>(i * 31u + 7u);
    }

    a.transport->send_on_channel_to(
        "data", b.id(), payload.data(), payload.size());

    ASSERT_TRUE(b.received.wait_for_count(1, 15s));
    auto items = b.received.snapshot();
    ASSERT_EQ(items.size(), 1u);
    ASSERT_EQ(items.front().bytes.size(), kSize);
    EXPECT_EQ(items.front().bytes, payload);
}

// =============================================================================
// P2 — Robustness / edge cases
// =============================================================================

// 12. send_on_channel_to called before the channel is open must not crash.
//     The earlier send may or may not be dropped depending on exact timing,
//     but Transport state must remain valid.
TEST_F(DatachannelTransportTest, SendBeforeChannelOpen_NoCrash)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    // Wait only for discovery, NOT for channel open.
    ASSERT_TRUE(a.joined.wait_for_count(1));

    const std::vector<uint8_t> early_payload { 0xAA, 0xBB, 0xCC };

    // This call must not crash regardless of whether the channel is open.
    a.transport->send_on_channel_to(
        "data", b.id(), early_payload.data(), early_payload.size());

    // Now finish rendezvous and send a known-good packet. If the earlier
    // send corrupted any state, this second send would fail to deliver.
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> real_payload { 0x42, 0x43 };
    a.transport->send_on_channel_to(
        "data", b.id(), real_payload.data(), real_payload.size());

    ASSERT_TRUE(b.received.wait_for_count(1));
    // real_payload must be the last item received (earlier dropped packets
    // may or may not appear depending on timing).
    auto items = b.received.snapshot();
    EXPECT_EQ(items.back().bytes, real_payload);
}

// 13. send_on_channel_to for an unknown peer id is a no-op.
TEST_F(DatachannelTransportTest, SendToUnknownPeer_NoCrash)
{
    PeerNode a;
    ASSERT_TRUE(a.connect(server.ws_url()));

    const std::vector<uint8_t> payload { 1, 2, 3 };
    // Must silently return — no throw, no crash.
    a.transport->send_on_channel_to(
        "data", "nonexistent_peer_id", payload.data(), payload.size());

    // Transport should still be usable afterwards.
    PeerNode b;
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    a.transport->send_on_channel_to(
        "data", b.id(), payload.data(), payload.size());
    ASSERT_TRUE(b.received.wait_for_count(1));
}

// 14. disconnect() followed by connect() on the same Transport works.
TEST_F(DatachannelTransportTest, DisconnectReconnect_Works)
{
    auto a = make_test_transport();
    test_util::EventCollector<std::string> a_joined;
    a->on_peer_joined(
        [&](const std::string& id) { a_joined.push(id); });

    ASSERT_TRUE(a->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*a));
    const std::string first_id = a->local_id();
    EXPECT_FALSE(first_id.empty());

    a->disconnect();
    EXPECT_FALSE(a->connected());
    EXPECT_EQ(a->local_id(), "");

    // Reconnect and verify a fresh peer can still rendezvous with A.
    ASSERT_TRUE(a->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*a));
    EXPECT_FALSE(a->local_id().empty());

    PeerNode b;
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(a_joined.wait_for_count(1));
    ASSERT_TRUE(b.joined.wait_for_count(1));
    EXPECT_EQ(b.joined.snapshot().front(), a->local_id());
}

// 15. connected() reflects the current WebSocket state.
TEST_F(DatachannelTransportTest, ConnectedFlag_ReflectsWsState)
{
    auto a = make_test_transport();
    EXPECT_FALSE(a->connected());

    ASSERT_TRUE(a->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*a));
    EXPECT_TRUE(a->connected());

    a->disconnect();
    EXPECT_FALSE(a->connected());
}

// =============================================================================
// Helper: raw rtc::WebSocket client. Bypasses Transport so tests can inject
// hand-crafted signaling messages that Transport itself would never emit.
// Used by CandidateBeforeOffer_NoCrash and MalformedJson_ServerStaysUp.
// =============================================================================
namespace {

struct RawSignalingClient {
    std::shared_ptr<rtc::WebSocket> ws;
    test_util::Waiter open;
    test_util::EventCollector<std::string> ids; // capture "welcome" → id

    RawSignalingClient()
        : ws(std::make_shared<rtc::WebSocket>())
    {
        ws->onOpen([this]() { open.signal(); });
        ws->onMessage([this](auto msg) {
            if (auto* s = std::get_if<std::string>(&msg)) {
                try {
                    auto j = nlohmann::json::parse(*s);
                    if (j.value("type", "") == "welcome") {
                        std::string welcome_id = j["id"];
                        ids.push(std::move(welcome_id));
                    }
                } catch (...) {
                    // ignore malformed server messages — not the point of
                    // the tests that use this helper
                }
            }
        });
    }

    bool connect(const std::string& url)
    {
        ws->open(url);
        if (!open.wait_for()) {
            return false;
        }
        return ids.wait_for_count(1);
    }

    std::string id() const
    {
        auto snap = ids.snapshot();
        return snap.empty() ? std::string { } : snap.front();
    }

    void send(const std::string& payload) { ws->send(payload); }
    void close() { ws->close(); }
};

} // namespace

// 16. Regression for a null-pc deref in Transport::handle_candidate. A
//     malicious/buggy peer may deliver a `candidate` message for a peer-id
//     that the local client only knows about via `peer_joined` (no offer
//     yet exchanged ⇒ PeerState::pc is a default-constructed shared_ptr).
//     Before the fix this dereferenced a null shared_ptr and segfaulted.
//     After the fix it is a logged no-op and Transport remains usable.
TEST_F(DatachannelTransportTest, CandidateBeforeOffer_NoCrash)
{
    PeerNode a;
    ASSERT_TRUE(a.connect(server.ws_url()));

    // Raw client registers with the server (so `a` pre-registers it via
    // peer_joined) but never emits an offer.
    RawSignalingClient raw;
    ASSERT_TRUE(raw.connect(server.ws_url()));

    ASSERT_TRUE(a.joined.wait_for_count(1));
    EXPECT_EQ(a.joined.snapshot().front(), raw.id());

    nlohmann::json cand;
    cand["type"] = "candidate";
    cand["to"] = a.id();
    cand["candidate"] = "candidate:1 1 UDP 2130706431 127.0.0.1 12345 typ host";
    cand["sdpMid"] = "0";
    raw.send(cand.dump());

    // Synthetic answer on the same path — also previously dereferenced
    // a null pc (handle_answer had the same bug).
    nlohmann::json ans;
    ans["type"] = "answer";
    ans["to"] = a.id();
    ans["sdp"] = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n";
    raw.send(ans.dump());

    // Give the messages time to be delivered and processed. If the fix
    // is not in place, the Transport's WebSocket handler thread crashes
    // and the test process dies before this sleep returns.
    std::this_thread::sleep_for(200ms);

    // Transport must still be usable afterwards — verify with a normal peer.
    raw.close();
    PeerNode b;
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> payload { 0x42 };
    a.transport->send_on_channel_to(
        "data", b.id(), payload.data(), payload.size());
    ASSERT_TRUE(b.received.wait_for_count(1));
    EXPECT_EQ(b.received.snapshot().front().bytes, payload);
}

// =============================================================================
// P2 — Additional coverage for signaling paths the original tests missed.
// =============================================================================

// 17. welcome.streaming_peers path. A joins and starts streaming; then B
//     joins. B's `on_streaming_started` must fire for A via the welcome
//     message (not via a live streaming_start broadcast). The original
//     StreamingSignals_PropagateToOtherPeers test only covers the live-
//     broadcast path, so this closes that gap.
TEST_F(DatachannelTransportTest, WelcomeIncludesStreamingPeers)
{
    PeerNode a;
    ASSERT_TRUE(a.connect(server.ws_url()));

    // Start streaming on A while no other peer is connected. This has to
    // reach the server state (`streaming_peers_` set) before B joins.
    a.transport->send_streaming_start();

    // Poll until the server has registered A as streaming. There is no
    // direct observer of that server-side set, so we rely on the fact
    // that the next welcome (sent to B) must include A. If the state
    // has not yet propagated, B's collector will stay empty and we fail.
    PeerNode b;
    ASSERT_TRUE(b.connect(server.ws_url()));

    ASSERT_TRUE(b.streaming_started.wait_for_count(1));
    EXPECT_EQ(b.streaming_started.snapshot().front(), a.id());

    // A must NOT have seen its own streaming_start echoed back.
    EXPECT_EQ(a.streaming_started.size(), 0u);
}

// 18. The "answerer → offerer" direction of send_on_channel_to. All other
//     tests send from the freshly-joined (offerer) side; this one sends
//     from the previously-connected (answerer) side whose DataChannel was
//     created inside `onDataChannel` rather than `createDataChannel`.
TEST_F(DatachannelTransportTest, SendOnChannelFromAnswererSide)
{
    PeerNode a; // will be the answerer for b's offer
    PeerNode b; // will be the offerer

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    // Send from a (answerer) to b (offerer).
    const std::vector<uint8_t> payload { 0x5A, 0x5B, 0x5C, 0x5D };
    a.transport->send_on_channel_to(
        "data", b.id(), payload.data(), payload.size());

    ASSERT_TRUE(b.received.wait_for_count(1));
    auto items = b.received.snapshot();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front().peer, a.id());
    EXPECT_EQ(items.front().bytes, payload);
}

// 19. Full three-peer mesh. Every unordered pair must form a working
//     DataChannel in both directions. SendOnChannel_BroadcastToAllPeers
//     only verified a→{b,c}; this one verifies b→{a,c} and c→{a,b}
//     as well, catching any bug where two non-initiators fail to pair.
TEST_F(DatachannelTransportTest, FullMesh_ThreePeers)
{
    PeerNode a, b, c;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(c.connect(server.ws_url()));

    // Every node must see exactly 2 peer_joined events and 2 channel
    // opens — one for each remote peer.
    ASSERT_TRUE(a.joined.wait_for_count(2));
    ASSERT_TRUE(b.joined.wait_for_count(2));
    ASSERT_TRUE(c.joined.wait_for_count(2));

    ASSERT_TRUE(a.channel_open_events.wait_for_count(2));
    ASSERT_TRUE(b.channel_open_events.wait_for_count(2));
    ASSERT_TRUE(c.channel_open_events.wait_for_count(2));

    // Unicast from each peer to each other peer. Using distinct payloads
    // per source lets each receiver verify exactly which sender it saw.
    const std::vector<uint8_t> from_a { 0xAA };
    const std::vector<uint8_t> from_b { 0xBB };
    const std::vector<uint8_t> from_c { 0xCC };

    a.transport->send_on_channel_to(
        "data", b.id(), from_a.data(), from_a.size());
    a.transport->send_on_channel_to(
        "data", c.id(), from_a.data(), from_a.size());
    b.transport->send_on_channel_to(
        "data", a.id(), from_b.data(), from_b.size());
    b.transport->send_on_channel_to(
        "data", c.id(), from_b.data(), from_b.size());
    c.transport->send_on_channel_to(
        "data", a.id(), from_c.data(), from_c.size());
    c.transport->send_on_channel_to(
        "data", b.id(), from_c.data(), from_c.size());

    // Each peer receives exactly 2 messages (one from each of the others).
    ASSERT_TRUE(a.received.wait_for_count(2));
    ASSERT_TRUE(b.received.wait_for_count(2));
    ASSERT_TRUE(c.received.wait_for_count(2));

    auto bytes_from = [](const std::vector<ReceivedPacket>& items,
                          const std::string& peer_id) -> std::vector<uint8_t> {
        for (const auto& p : items) {
            if (p.peer == peer_id)
                return p.bytes;
        }
        return { };
    };

    auto a_items = a.received.snapshot();
    auto b_items = b.received.snapshot();
    auto c_items = c.received.snapshot();

    EXPECT_EQ(a_items.size(), 2u);
    EXPECT_EQ(b_items.size(), 2u);
    EXPECT_EQ(c_items.size(), 2u);

    EXPECT_EQ(bytes_from(a_items, b.id()), from_b);
    EXPECT_EQ(bytes_from(a_items, c.id()), from_c);
    EXPECT_EQ(bytes_from(b_items, a.id()), from_a);
    EXPECT_EQ(bytes_from(b_items, c.id()), from_c);
    EXPECT_EQ(bytes_from(c_items, a.id()), from_a);
    EXPECT_EQ(bytes_from(c_items, b.id()), from_b);
}

// 20. Malformed JSON from a misbehaving client must not crash the signaling
//     server. After the bogus message the server must still be able to
//     accept a regular Transport rendezvous.
TEST_F(DatachannelTransportTest, MalformedJson_ServerStaysUp)
{
    RawSignalingClient raw;
    ASSERT_TRUE(raw.connect(server.ws_url()));

    // A stream of garbage that the server's json::parse cannot handle.
    raw.send("{not json");
    raw.send("not even close");
    raw.send(std::string { '\x01', '\x02', '\x03' });

    // Also an unknown type with a valid "to" field — server should route
    // it (silently dropped on client side) without crashing.
    nlohmann::json unknown;
    unknown["type"] = "there_is_no_such_type";
    unknown["to"] = "phantom_peer_id";
    raw.send(unknown.dump());

    std::this_thread::sleep_for(100ms);

    // Now check the server still works by running a normal rendezvous.
    PeerNode a, b;
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> payload { 0x01, 0x02 };
    a.transport->send_on_channel_to(
        "data", b.id(), payload.data(), payload.size());
    ASSERT_TRUE(b.received.wait_for_count(1));
    EXPECT_EQ(b.received.snapshot().front().bytes, payload);

    raw.close();
}
