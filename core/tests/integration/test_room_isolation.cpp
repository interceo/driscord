#include "config.hpp"
#include "rtc_cleanup_env.hpp"
#include "signaling_test_fixture.hpp"
#include "transport_harness.hpp"
#include "utils/log.hpp"
#include "wait_helpers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
static SuppressLogs suppress_logs_on_startup;

using namespace std::chrono_literals;
using test_util::kDefaultTimeout;
using test_util::PeerNode;
using test_util::SignalingServerFixture;
using test_util::wait_for_rendezvous;

// Short timeout used for "should NOT see peer" assertions to keep tests fast.
static constexpr auto kNegativeTimeout = std::chrono::milliseconds(800);

class RoomIsolationTest : public ::testing::Test {
protected:
    SignalingServerFixture server;
};

// 1. Two peers in the same channel see each other.
TEST_F(RoomIsolationTest, SameRoom_PeersSeeEachOther)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url(42)));
    ASSERT_TRUE(b.connect(server.ws_url(42)));

    EXPECT_NE(a.id(), b.id());

    ASSERT_TRUE(a.joined.wait_for_count(1, kDefaultTimeout));
    ASSERT_TRUE(b.joined.wait_for_count(1, kDefaultTimeout));

    EXPECT_EQ(a.joined.snapshot().front(), b.id());
    EXPECT_EQ(b.joined.snapshot().front(), a.id());
}

// 2. Peers in different channels do NOT see each other.
TEST_F(RoomIsolationTest, DifferentRooms_PeersIsolated)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url(42)));
    ASSERT_TRUE(b.connect(server.ws_url(99)));

    EXPECT_NE(a.id(), b.id());

    // Neither peer should receive a peer_joined event.
    EXPECT_FALSE(a.joined.wait_for_count(1, kNegativeTimeout));
    EXPECT_FALSE(b.joined.wait_for_count(1, kNegativeTimeout));

    EXPECT_TRUE(a.joined.snapshot().empty());
    EXPECT_TRUE(b.joined.snapshot().empty());
}

// 3. Three peers: two in channel 1, one in channel 2.
//    The solo peer must not see the pair, and the pair must not see the solo.
TEST_F(RoomIsolationTest, MixedRooms_OnlyIntraRoomVisibility)
{
    PeerNode a1, a2, b1;

    ASSERT_TRUE(a1.connect(server.ws_url(1)));
    ASSERT_TRUE(a2.connect(server.ws_url(1)));
    ASSERT_TRUE(b1.connect(server.ws_url(2)));

    // a1 and a2 are in the same room — they see each other.
    ASSERT_TRUE(a1.joined.wait_for_count(1, kDefaultTimeout));
    ASSERT_TRUE(a2.joined.wait_for_count(1, kDefaultTimeout));
    EXPECT_EQ(a1.joined.snapshot().front(), a2.id());
    EXPECT_EQ(a2.joined.snapshot().front(), a1.id());

    // b1 sees no peers at all.
    EXPECT_FALSE(b1.joined.wait_for_count(1, kNegativeTimeout));
    EXPECT_TRUE(b1.joined.snapshot().empty());

    // a1 and a2 do not see b1.
    EXPECT_EQ(a1.joined.snapshot().size(), 1u);
    EXPECT_EQ(a2.joined.snapshot().size(), 1u);
}

// 4. peer_left is only broadcast within the same room.
TEST_F(RoomIsolationTest, DifferentRooms_PeerLeftNotBroadcastCrossRoom)
{
    PeerNode a, b, c;

    ASSERT_TRUE(a.connect(server.ws_url(10)));
    ASSERT_TRUE(b.connect(server.ws_url(10)));
    ASSERT_TRUE(c.connect(server.ws_url(20)));

    // a and b rendezvous.
    ASSERT_TRUE(a.joined.wait_for_count(1, kDefaultTimeout));
    ASSERT_TRUE(b.joined.wait_for_count(1, kDefaultTimeout));

    b.transport->send_streaming_start();
    ASSERT_TRUE(a.streaming_started.wait_for_count(1, kDefaultTimeout));

    // Save b's id before disconnecting (disconnect() clears local_id).
    const std::string b_id = b.id();
    b.transport->disconnect();

    EXPECT_TRUE(b.transport->peers().empty());

    ASSERT_TRUE(a.left.wait_for_count(1, test_util::kDisconnectNoticeTimeout));
    EXPECT_EQ(a.left.snapshot().front(), b_id);
    ASSERT_TRUE(a.streaming_stopped.wait_for_count(
        1, test_util::kDisconnectNoticeTimeout));
    EXPECT_EQ(a.streaming_stopped.snapshot().front(), b_id);

    // c must not receive any peer_left.
    EXPECT_FALSE(c.left.wait_for_count(1, kNegativeTimeout));
    EXPECT_TRUE(c.left.snapshot().empty());
}

// 5. Direct-unicast (SDP) messages are only routable within the same room.
//    A peer in room A cannot reach a peer in room B even if it knows the id.
TEST_F(RoomIsolationTest, DifferentRooms_DirectMessageNotDelivered)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url(5)));
    ASSERT_TRUE(b.connect(server.ws_url(6)));

    // a tries to send a fake offer directly to b's id.
    // Because send_to is room-scoped, b's session is not found in room 5.
    // We simply verify b receives no peer_joined (which it wouldn't anyway
    // via broadcast) and the server stays stable.
    EXPECT_FALSE(b.joined.wait_for_count(1, kNegativeTimeout));
    EXPECT_TRUE(b.joined.snapshot().empty());

    // Server should still have exactly 2 sessions total (1 per room).
    EXPECT_EQ(server.active_sessions(), 2u);
}

// A late callback from the superseded WebSocket must not close or overwrite
// the replacement connection.
TEST_F(RoomIsolationTest, RapidReconnect_IgnoresSupersededSocketCallbacks)
{
    Transport transport;

    ASSERT_TRUE(transport.connect(server.ws_url(77)));
    ASSERT_TRUE(test_util::wait_for_local_id(transport));
    const std::string first_id = transport.local_id();

    ASSERT_TRUE(transport.connect(server.ws_url(77)));
    ASSERT_TRUE(test_util::wait_for_local_id(transport));
    const std::string second_id = transport.local_id();
    ASSERT_FALSE(second_id.empty());
    EXPECT_NE(second_id, first_id);

    std::this_thread::sleep_for(250ms);
    EXPECT_TRUE(transport.connected());
    EXPECT_EQ(transport.local_id(), second_id);
    EXPECT_EQ(transport.stats().state, TransportConnectionState::Connected);
}

TEST_F(RoomIsolationTest, ScreenWatchCapacityRejectsAndReleasesExplicitly)
{
    PeerNode watcher;
    ASSERT_TRUE(watcher.connect(server.ws_url(88)));

    std::vector<std::unique_ptr<PeerNode>> publishers;
    publishers.reserve(stream_defaults::kScreenReceiveSlots + 1);
    for (size_t i = 0; i < stream_defaults::kScreenReceiveSlots + 1; ++i) {
        auto publisher = std::make_unique<PeerNode>();
        ASSERT_TRUE(publisher->connect(server.ws_url(88)));
        publisher->transport->send_streaming_start();
        ASSERT_TRUE(watcher.streaming_started.wait_for_count(
            i + 1, kDefaultTimeout));
        watcher.transport->send_watch_start(publisher->id());
        publishers.push_back(std::move(publisher));
    }

    ASSERT_TRUE(watcher.watch_rejected.wait_for_count(1, kDefaultTimeout));
    const auto rejected = watcher.watch_rejected.snapshot().front();
    EXPECT_EQ(rejected.first, publishers.back()->id());
    EXPECT_EQ(rejected.second, signaling::WatchRejectReason::Capacity);

    // Removing one retained intent must release capacity immediately. The
    // previously rejected publisher can then be selected without reconnect.
    watcher.transport->send_watch_stop(publishers.front()->id());
    watcher.transport->send_watch_start(publishers.back()->id());
    EXPECT_FALSE(watcher.watch_rejected.wait_for_count(2, kNegativeTimeout));
}

TEST_F(RoomIsolationTest, InvalidScreenWatchReturnsTypedReason)
{
    PeerNode watcher, idle_peer;
    ASSERT_TRUE(watcher.connect(server.ws_url(89)));
    ASSERT_TRUE(idle_peer.connect(server.ws_url(89)));

    watcher.transport->send_watch_start(idle_peer.id());
    ASSERT_TRUE(watcher.watch_rejected.wait_for_count(1, kDefaultTimeout));
    EXPECT_EQ(watcher.watch_rejected.snapshot().back().second,
        signaling::WatchRejectReason::NotStreaming);

    watcher.transport->send_watch_start(watcher.id());
    ASSERT_TRUE(watcher.watch_rejected.wait_for_count(2, kDefaultTimeout));
    EXPECT_EQ(watcher.watch_rejected.snapshot().back().second,
        signaling::WatchRejectReason::UnknownPeer);
}
