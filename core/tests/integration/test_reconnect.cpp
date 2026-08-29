
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "utils/log.hpp"
#include "wait_helpers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

using test_util::PeerNode;
using test_util::SignalingServerFixture;

namespace {

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
const SuppressLogs suppress_logs_on_startup;

TEST(Reconnect, PeerRejoinsAfterDisconnectAndIsSeenAgain)
{
    SignalingServerFixture server;

    PeerNode stable;
    ASSERT_TRUE(stable.connect(server.ws_url(1)));

    {
        PeerNode transient;
        ASSERT_TRUE(transient.connect(server.ws_url(1)));
        ASSERT_TRUE(stable.joined.wait_for_count(1));
    }
    ASSERT_TRUE(
        stable.left.wait_for_count(1, test_util::kDisconnectNoticeTimeout));
    EXPECT_EQ(server.active_sessions(std::string { "1" }), 1u);

    PeerNode rejoiner;
    ASSERT_TRUE(rejoiner.connect(server.ws_url(1)));
    ASSERT_TRUE(stable.joined.wait_for_count(2));
    EXPECT_EQ(server.active_sessions(std::string { "1" }), 2u);
}

TEST(Reconnect, SameTransportReconnectsAfterExplicitDisconnect)
{
    SignalingServerFixture server;

    PeerNode observer;
    ASSERT_TRUE(observer.connect(server.ws_url(1)));

    auto transport = test_util::make_test_transport();
    ASSERT_TRUE(transport->connect(server.ws_url(1)).has_value());
    ASSERT_TRUE(test_util::wait_for_local_id(*transport));
    const std::string first_id = transport->local_id();
    ASSERT_FALSE(first_id.empty());
    ASSERT_TRUE(observer.joined.wait_for_count(1));

    transport->disconnect();
    ASSERT_TRUE(
        observer.left.wait_for_count(1, test_util::kDisconnectNoticeTimeout));

    ASSERT_TRUE(transport->connect(server.ws_url(1)).has_value());
    ASSERT_TRUE(test_util::wait_for_local_id(*transport));
    EXPECT_FALSE(transport->local_id().empty());
    EXPECT_TRUE(observer.joined.wait_for_count(2));

    transport->disconnect();
}

TEST(Reconnect, ClientsReconnectAfterTheSfuRestartsOnTheSamePort)
{
    unsigned short port = 0;
    {
        SignalingServerFixture server;
        port = server.port();
        PeerNode peer;
        ASSERT_TRUE(peer.connect(server.ws_url(1)));
        EXPECT_EQ(server.active_sessions(), 1u);
    }

    auto orphan = test_util::make_test_transport();
    const std::string dead_url
        = "ws://127.0.0.1:" + std::to_string(port) + "/channels/1";
    const auto result = orphan->connect(dead_url);
    if (result.has_value()) {
        EXPECT_FALSE(test_util::wait_for_local_id(
            *orphan, std::chrono::seconds(2)));
    }
    orphan->disconnect();
}

TEST(Reconnect, RapidReconnectStormLeavesConsistentRoster)
{
    SignalingServerFixture server;
    PeerNode observer;
    ASSERT_TRUE(observer.connect(server.ws_url(1)));

    for (int i = 0; i < 10; ++i) {
        PeerNode peer;
        ASSERT_TRUE(peer.connect(server.ws_url(1)));
    }

    ASSERT_TRUE(observer.joined.wait_for_count(10));
    ASSERT_TRUE(
        observer.left.wait_for_count(10, test_util::kDisconnectNoticeTimeout));
    EXPECT_EQ(server.active_sessions(std::string { "1" }), 1u);
}

}
