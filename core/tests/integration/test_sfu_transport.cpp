#include "channel_labels.hpp"
#include "rtc_cleanup_env.hpp"
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
// Media flows client → server → clients. Each client holds exactly one
// PeerConnection, and it terminates at the server; there is no peer mesh and
// no SDP is ever exchanged between two clients.
//
// The fixture is a member so it is constructed BEFORE test-body locals and
// destroyed AFTER them — RAII order ensures Transports tear down against a
// still-alive server.
// =============================================================================
class SfuTransportTest : public ::testing::Test {
protected:
    SignalingServerFixture server;
};

// -----------------------------------------------------------------------------
// Roster / signaling
// -----------------------------------------------------------------------------

// 1. Two clients join and discover each other through the signaling server.
TEST_F(SfuTransportTest, Rendezvous_TwoPeers)
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

// 2. The media path to the server comes up on its own, without any peer
//    being present — it is a connection to the server, not to another client.
TEST_F(SfuTransportTest, MediaChannel_OpensAgainstServerAlone)
{
    PeerNode a;
    ASSERT_TRUE(a.connect(server.ws_url()));
    EXPECT_TRUE(a.channel_open.wait_for());
}

// 3. peer_left reaches the remaining peer.
TEST_F(SfuTransportTest, PeerLeft_FiresOnRemainingPeer)
{
    PeerNode a;
    ASSERT_TRUE(a.connect(server.ws_url()));
    {
        PeerNode b;
        ASSERT_TRUE(b.connect(server.ws_url()));
        ASSERT_TRUE(wait_for_rendezvous(a, b));
    }
    ASSERT_TRUE(a.left.wait_for_count(1));
    EXPECT_FALSE(a.left.snapshot().front().empty());
    EXPECT_TRUE(a.transport->peers().empty());
}

// 4. Repeated connect/disconnect neither leaks nor hangs.
TEST(SfuTransport, RepeatedConnectDisconnect_NoLeaksOrHangs)
{
    SignalingServerFixture server;
    for (int i = 0; i < 3; ++i) {
        PeerNode a, b;
        ASSERT_TRUE(a.connect(server.ws_url()));
        ASSERT_TRUE(b.connect(server.ws_url()));
        ASSERT_TRUE(wait_for_rendezvous(a, b)) << "iteration " << i;
    }
}

// 5. disconnect() followed by connect() on the same Transport works.
TEST_F(SfuTransportTest, DisconnectReconnect_Works)
{
    PeerNode a;
    ASSERT_TRUE(a.connect(server.ws_url()));
    const std::string first_id = a.id();
    EXPECT_FALSE(first_id.empty());

    a.transport->disconnect();
    EXPECT_FALSE(a.transport->connected());
    EXPECT_EQ(a.transport->local_id(), "");

    ASSERT_TRUE(a.connect(server.ws_url()));
    EXPECT_FALSE(a.id().empty());

    PeerNode b;
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(b.joined.wait_for_count(1));
    EXPECT_EQ(b.joined.snapshot().front(), a.id());
}

// 6. connected() reflects the current WebSocket state.
TEST_F(SfuTransportTest, ConnectedFlag_ReflectsWsState)
{
    auto a = make_test_transport();
    EXPECT_FALSE(a->connected());

    ASSERT_TRUE(a->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*a));
    EXPECT_TRUE(a->connected());

    a->disconnect();
    EXPECT_FALSE(a->connected());
}

// -----------------------------------------------------------------------------
// Media fan-out through the server
// -----------------------------------------------------------------------------

// 7. A payload sent once to the server is delivered to the other peer,
//    byte-exact and attributed to the sender.
TEST_F(SfuTransportTest, Send_ReachesOtherPeerViaServer)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> payload { 0xDE, 0xAD, 0xBE, 0xEF };
    a.send(payload);

    ASSERT_TRUE(b.received.wait_for_count(1));
    auto items = b.received.snapshot();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front().peer, a.id());
    EXPECT_EQ(items.front().bytes, payload);

    // The sender must not receive its own packet back.
    std::this_thread::sleep_for(100ms);
    EXPECT_TRUE(a.received.snapshot().empty());
}

// 8. One send reaches every other peer in the room — the server does the
//    fan-out, the client transmits a single copy.
TEST_F(SfuTransportTest, Send_FansOutToAllPeers)
{
    PeerNode a, b, c;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(c.connect(server.ws_url()));

    ASSERT_TRUE(a.joined.wait_for_count(2));
    ASSERT_TRUE(b.channel_open.wait_for());
    ASSERT_TRUE(c.channel_open.wait_for());
    ASSERT_TRUE(a.channel_open.wait_for());

    const std::vector<uint8_t> payload { 0x11, 0x22 };
    a.send(payload);

    ASSERT_TRUE(b.received.wait_for_count(1));
    ASSERT_TRUE(c.received.wait_for_count(1));
    EXPECT_EQ(b.received.snapshot().front().bytes, payload);
    EXPECT_EQ(c.received.snapshot().front().bytes, payload);
    EXPECT_EQ(b.received.snapshot().front().peer, a.id());
    EXPECT_EQ(c.received.snapshot().front().peer, a.id());
}

// 9. Channels stay independent: a payload sent on one label is not delivered
//    on another.
TEST_F(SfuTransportTest, MultipleChannels_RoutedIndependently)
{
    PeerNode a("data"), b("data");
    auto a_extra = a.add_channel("extra");
    auto b_extra = b.add_channel("extra");

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> on_data { 0x01 };
    const std::vector<uint8_t> on_extra { 0x02 };
    a.send(on_data);
    a.send_on("extra", on_extra);

    ASSERT_TRUE(b.received.wait_for_count(1));
    ASSERT_TRUE(b_extra->wait_for_count(1));

    EXPECT_EQ(b.received.snapshot().front().bytes, on_data);
    EXPECT_EQ(b_extra->snapshot().front().bytes, on_extra);
    EXPECT_EQ(b.received.snapshot().size(), 1u);
    EXPECT_EQ(b_extra->snapshot().size(), 1u);
    EXPECT_TRUE(a_extra->snapshot().empty());
}

// 10. A payload far larger than one MTU survives the round trip: it is sent
//     as a single message and fragmented by SCTP, with no application-level
//     chunking involved.
TEST_F(SfuTransportTest, LargePayload_FragmentedBySctp)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    constexpr size_t kSize = 200 * 1024;
    std::vector<uint8_t> payload(kSize);
    for (size_t i = 0; i < kSize; ++i) {
        payload[i] = static_cast<uint8_t>(i * 31u + 7u);
    }

    a.send(payload);

    ASSERT_TRUE(b.received.wait_for_count(1, 20s));
    auto items = b.received.snapshot();
    ASSERT_EQ(items.size(), 1u);
    ASSERT_EQ(items.front().bytes.size(), kSize);
    EXPECT_EQ(items.front().bytes, payload);
}

// 11. Sending before the media path is up must not crash, and must not
//     corrupt the transport for later sends.
TEST_F(SfuTransportTest, SendBeforeChannelOpen_NoCrash)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));

    const std::vector<uint8_t> early { 0xAA, 0xBB };
    a.send(early); // may or may not be dropped depending on timing

    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> real { 0x42, 0x43 };
    a.send(real);

    ASSERT_TRUE(b.received.wait_for_count(1));
    EXPECT_EQ(b.received.snapshot().back().bytes, real);
}

// -----------------------------------------------------------------------------
// Screen-share gating
// -----------------------------------------------------------------------------

// 12. Screen video only reaches peers that asked to watch. Voice reaches
//     everyone regardless — the server gates on the channel label.
TEST_F(SfuTransportTest, ScreenVideo_OnlyReachesWatchers)
{
    PeerNode streamer(channel::kVideo);
    PeerNode watcher(channel::kVideo);
    PeerNode bystander(channel::kVideo);

    auto streamer_audio = streamer.add_channel(channel::kAudio);
    auto watcher_audio = watcher.add_channel(channel::kAudio);
    auto bystander_audio = bystander.add_channel(channel::kAudio);
    (void)streamer_audio;

    ASSERT_TRUE(streamer.connect(server.ws_url()));
    ASSERT_TRUE(watcher.connect(server.ws_url()));
    ASSERT_TRUE(bystander.connect(server.ws_url()));

    ASSERT_TRUE(streamer.channel_open.wait_for());
    ASSERT_TRUE(watcher.channel_open.wait_for());
    ASSERT_TRUE(bystander.channel_open.wait_for());
    ASSERT_TRUE(streamer.joined.wait_for_count(2));

    watcher.transport->send_watch_start();
    // Let the server register the watcher before the media races it.
    std::this_thread::sleep_for(150ms);

    const std::vector<uint8_t> frame { 0x01, 0x02, 0x03 };
    streamer.send(frame);

    ASSERT_TRUE(watcher.received.wait_for_count(1));
    EXPECT_EQ(watcher.received.snapshot().front().bytes, frame);

    std::this_thread::sleep_for(200ms);
    EXPECT_TRUE(bystander.received.snapshot().empty())
        << "screen video reached a peer that never sent watch_start";

    // Voice is not gated: it reaches the bystander too.
    const std::vector<uint8_t> voice { 0x77 };
    streamer.send_on(channel::kAudio, voice);
    ASSERT_TRUE(bystander_audio->wait_for_count(1));
    ASSERT_TRUE(watcher_audio->wait_for_count(1));
    EXPECT_EQ(bystander_audio->snapshot().front().bytes, voice);

    // watch_stop takes the watcher back out of the fan-out set.
    watcher.transport->send_watch_stop();
    std::this_thread::sleep_for(150ms);
    streamer.send(frame);
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(watcher.received.snapshot().size(), 1u);
}

// -----------------------------------------------------------------------------
// Stream lifecycle signals
// -----------------------------------------------------------------------------

// 13. streaming_start / _stop propagate through the signaling server.
TEST_F(SfuTransportTest, StreamingSignals_PropagateToOtherPeers)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(a.joined.wait_for_count(1));
    ASSERT_TRUE(b.joined.wait_for_count(1));

    a.transport->send_streaming_start();
    ASSERT_TRUE(b.streaming_started.wait_for_count(1));
    EXPECT_EQ(b.streaming_started.snapshot().front(), a.id());

    a.transport->send_streaming_stop();
    ASSERT_TRUE(b.streaming_stopped.wait_for_count(1));
    EXPECT_EQ(b.streaming_stopped.snapshot().front(), a.id());

    EXPECT_EQ(a.streaming_started.size(), 0u);
    EXPECT_EQ(a.streaming_stopped.size(), 0u);
}

// 14. watch_start / _stop propagate too (the client-visible half; the
//     server-side effect is covered by ScreenVideo_OnlyReachesWatchers).
TEST_F(SfuTransportTest, WatchSignals_PropagateToOtherPeers)
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

// 15. A peer that started streaming before we joined is reported in welcome.
TEST_F(SfuTransportTest, WelcomeIncludesStreamingPeers)
{
    PeerNode a;
    ASSERT_TRUE(a.connect(server.ws_url()));
    a.transport->send_streaming_start();

    PeerNode b;
    ASSERT_TRUE(b.connect(server.ws_url()));

    ASSERT_TRUE(b.streaming_started.wait_for_count(1));
    EXPECT_EQ(b.streaming_started.snapshot().front(), a.id());
    EXPECT_EQ(a.streaming_started.size(), 0u);
}

// -----------------------------------------------------------------------------
// Identity
// -----------------------------------------------------------------------------

// 16. Usernames ride the signaling channel (`?u=` → welcome/peer_joined), so
//     they are known without any media channel or PeerConnection.
TEST_F(SfuTransportTest, Identity_PropagatesViaWelcomeAndPeerJoined)
{
    auto a = make_test_transport();
    ASSERT_TRUE(a->connect(server.ws_url() + "/channels/id-test?u=alice").has_value());
    ASSERT_TRUE(wait_for_local_id(*a));

    test_util::EventCollector<std::pair<std::string, std::string>> a_identities;
    a->on_peer_identity([&](const std::string& id, const std::string& name) {
        a_identities.push({ id, name });
    });

    auto b = make_test_transport();
    ASSERT_TRUE(b->connect(server.ws_url() + "/channels/id-test?u=bob").has_value());
    ASSERT_TRUE(wait_for_local_id(*b));

    // a learns b's name from peer_joined.
    ASSERT_TRUE(a_identities.wait_for_count(1));
    EXPECT_EQ(a_identities.snapshot().front().first, b->local_id());
    EXPECT_EQ(a_identities.snapshot().front().second, "bob");
    EXPECT_EQ(a->peer_username(b->local_id()), "bob");

    // b learns a's name from its own welcome.
    for (int i = 0; i < 50 && b->peer_username(a->local_id()).empty(); ++i) {
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(b->peer_username(a->local_id()), "alice");
}

// =============================================================================
// Raw signaling client: bypasses Transport so tests can observe exactly what
// the server does and does not send to a client.
// =============================================================================
namespace {

struct RawSignalingClient {
    std::shared_ptr<rtc::WebSocket> ws;
    test_util::Waiter open;
    test_util::EventCollector<std::string> ids;
    test_util::EventCollector<std::string> message_types;

    RawSignalingClient()
        : ws(std::make_shared<rtc::WebSocket>())
    {
        ws->onOpen([this]() { open.signal(); });
        ws->onMessage([this](auto msg) {
            auto* s = std::get_if<std::string>(&msg);
            if (!s) {
                return;
            }
            try {
                auto j = nlohmann::json::parse(*s);
                std::string type = j.value("type", "");
                message_types.push(type);
                if (type == "welcome") {
                    std::string welcome_id = j["id"];
                    ids.push(std::move(welcome_id));
                }
            } catch (...) {
                // ignore malformed server messages — not the point here
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

// 17. SDP never reaches another client. The server terminates the
//     PeerConnection itself, so a peer sharing the room must never observe an
//     offer/answer/candidate from anyone else.
TEST_F(SfuTransportTest, Sdp_IsConsumedByServer_NeverForwardedToPeers)
{
    RawSignalingClient observer;
    ASSERT_TRUE(observer.connect(server.ws_url()));

    PeerNode a;
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(a.channel_open.wait_for());

    std::this_thread::sleep_for(300ms);

    for (const auto& type : observer.message_types.snapshot()) {
        EXPECT_NE(type, "offer") << "SDP offer leaked to another client";
        EXPECT_NE(type, "answer") << "SDP answer leaked to another client";
        EXPECT_NE(type, "candidate") << "ICE candidate leaked to another client";
    }

    observer.close();
}

// 18. An ICE candidate arriving before any offer must not crash the server.
TEST_F(SfuTransportTest, CandidateBeforeOffer_ServerStaysUp)
{
    RawSignalingClient raw;
    ASSERT_TRUE(raw.connect(server.ws_url()));

    nlohmann::json cand;
    cand["type"] = "candidate";
    cand["candidate"] = "candidate:1 1 UDP 2130706431 127.0.0.1 12345 typ host";
    cand["sdpMid"] = "0";
    raw.send(cand.dump());

    std::this_thread::sleep_for(200ms);
    raw.close();

    // The server must still serve a normal session afterwards.
    PeerNode a, b;
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> payload { 0x42 };
    a.send(payload);
    ASSERT_TRUE(b.received.wait_for_count(1));
    EXPECT_EQ(b.received.snapshot().front().bytes, payload);
}

// 19. Malformed JSON from a misbehaving client must not take the server down.
TEST_F(SfuTransportTest, MalformedJson_ServerStaysUp)
{
    RawSignalingClient raw;
    ASSERT_TRUE(raw.connect(server.ws_url()));

    raw.send("{not json");
    raw.send("not even close");
    raw.send(std::string { '\x01', '\x02', '\x03' });

    nlohmann::json unknown;
    unknown["type"] = "there_is_no_such_type";
    unknown["to"] = "phantom_peer_id";
    raw.send(unknown.dump());

    std::this_thread::sleep_for(100ms);

    PeerNode a, b;
    ASSERT_TRUE(a.connect(server.ws_url()));
    ASSERT_TRUE(b.connect(server.ws_url()));
    ASSERT_TRUE(wait_for_rendezvous(a, b));

    const std::vector<uint8_t> payload { 0x01, 0x02 };
    a.send(payload);
    ASSERT_TRUE(b.received.wait_for_count(1));
    EXPECT_EQ(b.received.snapshot().front().bytes, payload);

    raw.close();
}
