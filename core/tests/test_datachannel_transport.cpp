#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "wait_helpers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

// Polls Transport::local_id() until it becomes non-empty (i.e. the server's
// "welcome" has been processed) or the timeout elapses.
bool wait_for_local_id(Transport& t,
    std::chrono::milliseconds timeout = test_util::kDefaultTimeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!t.local_id().empty()) {
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return !t.local_id().empty();
}

// Builds a Transport already configured for loopback-only ICE so tests do
// not depend on STUN reachability and ICE gathering completes promptly.
std::unique_ptr<Transport> make_test_transport()
{
    auto t = std::make_unique<Transport>();
    t->set_ice_servers({});
    return t;
}

} // namespace

TEST(DatachannelTransport, Rendezvous_TwoPeers)
{
    test_util::SignalingServerFixture server;

    test_util::EventCollector<std::string> t1_joined;
    test_util::EventCollector<std::string> t2_joined;

    auto t1 = make_test_transport();
    auto t2 = make_test_transport();

    t1->on_peer_joined([&](const std::string& id) { t1_joined.push(id); });
    t2->on_peer_joined([&](const std::string& id) { t2_joined.push(id); });

    ASSERT_TRUE(t1->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*t1));
    const std::string t1_id = t1->local_id();
    ASSERT_FALSE(t1_id.empty());

    ASSERT_TRUE(t2->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*t2));
    const std::string t2_id = t2->local_id();
    ASSERT_FALSE(t2_id.empty());
    ASSERT_NE(t1_id, t2_id);

    ASSERT_TRUE(t1_joined.wait_for_count(1));
    ASSERT_TRUE(t2_joined.wait_for_count(1));

    auto t1_seen = t1_joined.snapshot();
    auto t2_seen = t2_joined.snapshot();
    EXPECT_EQ(t1_seen.size(), 1u);
    EXPECT_EQ(t2_seen.size(), 1u);
    EXPECT_EQ(t1_seen.front(), t2_id);
    EXPECT_EQ(t2_seen.front(), t1_id);

    EXPECT_EQ(t1->peers().size(), 1u);
    EXPECT_EQ(t2->peers().size(), 1u);
    EXPECT_EQ(t1->peers().front().id, t2_id);
    EXPECT_EQ(t2->peers().front().id, t1_id);
}

TEST(DatachannelTransport, DataChannel_OpensOnBothEnds)
{
    test_util::SignalingServerFixture server;

    test_util::EventCollector<std::string> t1_open;
    test_util::EventCollector<std::string> t2_open;

    auto t1 = make_test_transport();
    auto t2 = make_test_transport();

    Transport::ChannelSpec spec1;
    spec1.label = "data";
    spec1.unordered = false;
    spec1.max_retransmits = -1;
    spec1.on_open = [&](const std::string& peer) { t1_open.push(peer); };
    t1->register_channel(spec1);

    Transport::ChannelSpec spec2;
    spec2.label = "data";
    spec2.unordered = false;
    spec2.max_retransmits = -1;
    spec2.on_open = [&](const std::string& peer) { t2_open.push(peer); };
    t2->register_channel(spec2);

    ASSERT_TRUE(t1->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*t1));
    ASSERT_TRUE(t2->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*t2));

    ASSERT_TRUE(t1_open.wait_for_count(1))
        << "T1 never saw 'data' channel open";
    ASSERT_TRUE(t2_open.wait_for_count(1))
        << "T2 never saw 'data' channel open";

    EXPECT_EQ(t1_open.snapshot().front(), t2->local_id());
    EXPECT_EQ(t2_open.snapshot().front(), t1->local_id());
}

TEST(DatachannelTransport, SendOnChannelTo_PointToPoint)
{
    test_util::SignalingServerFixture server;

    struct Received {
        std::string peer;
        std::vector<uint8_t> bytes;
    };
    test_util::EventCollector<Received> t2_received;
    test_util::Waiter t1_open;
    test_util::Waiter t2_open;

    auto t1 = make_test_transport();
    auto t2 = make_test_transport();

    Transport::ChannelSpec spec1;
    spec1.label = "data";
    spec1.unordered = false;
    spec1.max_retransmits = -1;
    spec1.on_open = [&](const std::string&) { t1_open.signal(); };
    t1->register_channel(spec1);

    Transport::ChannelSpec spec2;
    spec2.label = "data";
    spec2.unordered = false;
    spec2.max_retransmits = -1;
    spec2.on_open = [&](const std::string&) { t2_open.signal(); };
    spec2.on_data = [&](const std::string& peer,
                        const uint8_t* data,
                        size_t len) {
        t2_received.push(Received {
            peer, std::vector<uint8_t>(data, data + len) });
    };
    t2->register_channel(spec2);

    ASSERT_TRUE(t1->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*t1));
    ASSERT_TRUE(t2->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*t2));

    ASSERT_TRUE(t1_open.wait_for());
    ASSERT_TRUE(t2_open.wait_for());

    const std::vector<uint8_t> payload {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04
    };
    t1->send_on_channel_to(
        "data", t2->local_id(), payload.data(), payload.size());

    ASSERT_TRUE(t2_received.wait_for_count(1))
        << "T2 never received the payload";

    auto items = t2_received.snapshot();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front().peer, t1->local_id());
    EXPECT_EQ(items.front().bytes, payload);
}

TEST(DatachannelTransport, PeerLeft_FiresOnRemainingPeer)
{
    test_util::SignalingServerFixture server;

    test_util::EventCollector<std::string> t1_joined;
    test_util::EventCollector<std::string> t1_left;

    auto t1 = make_test_transport();
    auto t2 = make_test_transport();

    t1->on_peer_joined([&](const std::string& id) { t1_joined.push(id); });
    t1->on_peer_left([&](const std::string& id) { t1_left.push(id); });

    ASSERT_TRUE(t1->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*t1));
    ASSERT_TRUE(t2->connect(server.ws_url()).has_value());
    ASSERT_TRUE(wait_for_local_id(*t2));

    ASSERT_TRUE(t1_joined.wait_for_count(1));
    const std::string t2_id = t2->local_id();
    EXPECT_EQ(t1_joined.snapshot().front(), t2_id);

    // Tear down T2 so the signaling server fires peer_left to T1.
    t2.reset();

    ASSERT_TRUE(t1_left.wait_for_count(1))
        << "T1 never received peer_left for T2";
    EXPECT_EQ(t1_left.snapshot().front(), t2_id);

    // T1.peers() should drop the entry within a short window after the
    // callback fires (peers_ is updated synchronously inside on_ws_message
    // before the user callback is invoked).
    EXPECT_EQ(t1->peers().size(), 0u);
}

TEST(DatachannelTransport, RepeatedConnectDisconnect_NoLeaksOrHangs)
{
    constexpr int kIterations = 5;

    for (int i = 0; i < kIterations; ++i) {
        test_util::SignalingServerFixture server;

        test_util::Waiter t1_open;
        test_util::Waiter t2_open;

        auto t1 = make_test_transport();
        auto t2 = make_test_transport();

        Transport::ChannelSpec spec1;
        spec1.label = "data";
        spec1.unordered = false;
        spec1.max_retransmits = -1;
        spec1.on_open = [&](const std::string&) { t1_open.signal(); };
        t1->register_channel(spec1);

        Transport::ChannelSpec spec2;
        spec2.label = "data";
        spec2.unordered = false;
        spec2.max_retransmits = -1;
        spec2.on_open = [&](const std::string&) { t2_open.signal(); };
        t2->register_channel(spec2);

        ASSERT_TRUE(t1->connect(server.ws_url()).has_value())
            << "iteration " << i;
        ASSERT_TRUE(wait_for_local_id(*t1)) << "iteration " << i;
        ASSERT_TRUE(t2->connect(server.ws_url()).has_value())
            << "iteration " << i;
        ASSERT_TRUE(wait_for_local_id(*t2)) << "iteration " << i;

        ASSERT_TRUE(t1_open.wait_for()) << "iteration " << i;
        ASSERT_TRUE(t2_open.wait_for()) << "iteration " << i;

        // Destruction must be quick and clean — Transport's destructor calls
        // disconnect() which closes all DCs/PCs and the WS. The CTest TIMEOUT
        // catches deadlocks here.
        t1.reset();
        t2.reset();
    }
}
