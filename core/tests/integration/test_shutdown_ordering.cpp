
#include "signaling_test_fixture.hpp"
#include "transport_harness.hpp"
#include "utils/log.hpp"
#include "wait_helpers.hpp"

#include <gtest/gtest.h>
#include <rtc/rtc.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <vector>

using test_util::PeerNode;
using test_util::SignalingServerFixture;

namespace {

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
const SuppressLogs suppress_logs_on_startup;

void assert_cleanup_completes(std::chrono::seconds budget)
{
    auto done = std::async(std::launch::async, [] { rtc::Cleanup().wait(); });
    ASSERT_EQ(done.wait_for(budget), std::future_status::ready)
        << "rtc::Cleanup() did not finish within the budget — an object "
           "outlived its owner or teardown deadlocked";
}

TEST(ShutdownOrdering, RapidConnectDisconnectChurnCleansUp)
{
    {
        SignalingServerFixture server;
        for (int round = 0; round < 20; ++round) {
            PeerNode peer;
            ASSERT_TRUE(peer.connect(server.ws_url(1)));
        }
    }
    assert_cleanup_completes(std::chrono::seconds(10));
}

TEST(ShutdownOrdering, ServerDestroyedWithLiveSessionsCleansUp)
{
    std::vector<std::unique_ptr<PeerNode>> peers;
    {
        SignalingServerFixture server;
        for (int i = 0; i < 6; ++i) {
            auto peer = std::make_unique<PeerNode>();
            ASSERT_TRUE(peer->connect(server.ws_url(1)));
            peers.push_back(std::move(peer));
        }
    }
    peers.clear();
    assert_cleanup_completes(std::chrono::seconds(10));
}

TEST(ShutdownOrdering, OfferInFlightAtDisconnectCleansUp)
{
    {
        SignalingServerFixture server;
        for (int round = 0; round < 8; ++round) {
            PeerNode peer;
            ASSERT_TRUE(peer.connect(server.ws_url(1)));
            peer.transport->send_media_offer(
                signaling::ConnectionId::Voice,
                "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
                "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
                "a=rtpmap:111 opus/48000/2\r\na=mid:0\r\n"
                "a=sendonly\r\n");
        }
    }
    assert_cleanup_completes(std::chrono::seconds(10));
}

}
