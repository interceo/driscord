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

static constexpr auto kNegativeTimeout = std::chrono::milliseconds(800);

class RoomIsolationTest : public ::testing::Test {
protected:
    SignalingServerFixture server;
};

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

TEST_F(RoomIsolationTest, DifferentRooms_PeersIsolated)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url(42)));
    ASSERT_TRUE(b.connect(server.ws_url(99)));

    EXPECT_NE(a.id(), b.id());

    EXPECT_FALSE(a.joined.wait_for_count(1, kNegativeTimeout));
    EXPECT_FALSE(b.joined.wait_for_count(1, kNegativeTimeout));

    EXPECT_TRUE(a.joined.snapshot().empty());
    EXPECT_TRUE(b.joined.snapshot().empty());
}

TEST_F(RoomIsolationTest, MixedRooms_OnlyIntraRoomVisibility)
{
    PeerNode a1, a2, b1;

    ASSERT_TRUE(a1.connect(server.ws_url(1)));
    ASSERT_TRUE(a2.connect(server.ws_url(1)));
    ASSERT_TRUE(b1.connect(server.ws_url(2)));

    ASSERT_TRUE(a1.joined.wait_for_count(1, kDefaultTimeout));
    ASSERT_TRUE(a2.joined.wait_for_count(1, kDefaultTimeout));
    EXPECT_EQ(a1.joined.snapshot().front(), a2.id());
    EXPECT_EQ(a2.joined.snapshot().front(), a1.id());

    EXPECT_FALSE(b1.joined.wait_for_count(1, kNegativeTimeout));
    EXPECT_TRUE(b1.joined.snapshot().empty());

    EXPECT_EQ(a1.joined.snapshot().size(), 1u);
    EXPECT_EQ(a2.joined.snapshot().size(), 1u);
}

TEST_F(RoomIsolationTest, DifferentRooms_PeerLeftNotBroadcastCrossRoom)
{
    PeerNode a, b, c;

    ASSERT_TRUE(a.connect(server.ws_url(10)));
    ASSERT_TRUE(b.connect(server.ws_url(10)));
    ASSERT_TRUE(c.connect(server.ws_url(20)));

    ASSERT_TRUE(a.joined.wait_for_count(1, kDefaultTimeout));
    ASSERT_TRUE(b.joined.wait_for_count(1, kDefaultTimeout));

    b.transport->send_streaming_start();
    ASSERT_TRUE(a.streaming_started.wait_for_count(1, kDefaultTimeout));

    const std::string b_id = b.id();
    b.transport->disconnect();

    EXPECT_TRUE(b.transport->peers().empty());

    ASSERT_TRUE(a.left.wait_for_count(1, test_util::kDisconnectNoticeTimeout));
    EXPECT_EQ(a.left.snapshot().front(), b_id);
    ASSERT_TRUE(a.streaming_stopped.wait_for_count(
        1, test_util::kDisconnectNoticeTimeout));
    EXPECT_EQ(a.streaming_stopped.snapshot().front(), b_id);

    EXPECT_FALSE(c.left.wait_for_count(1, kNegativeTimeout));
    EXPECT_TRUE(c.left.snapshot().empty());
}

TEST_F(RoomIsolationTest, DifferentRooms_DirectMessageNotDelivered)
{
    PeerNode a, b;

    ASSERT_TRUE(a.connect(server.ws_url(5)));
    ASSERT_TRUE(b.connect(server.ws_url(6)));

    EXPECT_FALSE(b.joined.wait_for_count(1, kNegativeTimeout));
    EXPECT_TRUE(b.joined.snapshot().empty());

    EXPECT_EQ(server.active_sessions(), 2u);
}

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
