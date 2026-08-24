// Shutdown ordering under churn.
//
// libdatachannel keeps a process-wide thread pool and static state behind
// rtc::Cleanup(); the SFU spins a fresh MediaConnections (up to two
// PeerConnections) per session. The failure mode this guards is the one
// CLAUDE.md and rtc_cleanup_env.hpp both call out: destroying servers and
// sessions in the wrong order, or while negotiation is in flight, races the
// runtime teardown and crashes intermittently.
//
// Adapted from libdatachannel's own test_cleanup: rather than only running
// rtc::Cleanup() at process exit, this asserts it completes within a bound
// after heavy connect/disconnect churn — a hang or crash here is the bug.

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

// rtc::Cleanup() blocks until every libdatachannel object is destroyed. Run it
// on another thread so the test fails with a diagnostic instead of hanging the
// whole binary if teardown deadlocks.
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
            // Immediately disconnect via the node destructor at scope exit,
            // so sessions come and go faster than negotiation would settle.
        }
        // Server destructor runs here, before rtc::Cleanup below, which is the
        // production order in main.cpp.
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
        // Server is torn down while all six sessions are still connected.
    }
    // Now drop the clients, after the server is already gone.
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
            // Push an offer into the SFU, then tear the peer down at once so
            // the PeerConnection is created and destroyed back to back.
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

} // namespace
