#pragma once

#include "net_cond.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "wait_helpers.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace test_util {

// Polls Transport::local_id() until the server's "welcome" message has been
// processed or the timeout elapses. Returns true if the id is set.
inline bool wait_for_local_id(Transport& t,
    std::chrono::milliseconds timeout = kDefaultTimeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!t.local_id().empty()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return !t.local_id().empty();
}

// Constructs a Transport already configured for loopback ICE (empty ICE
// server list → host candidates only). This avoids STUN reachability
// delays in CI.
inline std::unique_ptr<Transport> make_test_transport()
{
    auto t = std::make_unique<Transport>();
    t->set_ice_servers({ });
    return t;
}

// Payload captured by an on_data callback — keeps the sender id alongside
// the bytes so tests can assert routing correctness.
struct ReceivedPacket {
    std::string peer;
    std::vector<uint8_t> bytes;
};

// Per-peer test harness: owns the Transport and the collectors that record
// the callbacks we care about for datachannel tests. Each harness registers
// one data channel with a configurable label. Use several instances in a
// single test body to simulate a small mesh.
//
// Typical usage:
//   PeerNode a("data");
//   PeerNode b("data");
//   a.connect(server.ws_url());
//   b.connect(server.ws_url());
//   a.wait_connected(); b.wait_connected();
//   wait_for_rendezvous(a, b);
//
// The destructor of the harness runs before the SignalingServerFixture in
// test bodies because of RAII declaration order (fixture declared first in
// the test body's scope → destructed last), which avoids races on teardown.
struct PeerNode {
    std::unique_ptr<Transport> transport;
    std::string label;

    // Non-null when this node was constructed with a NetProfile.
    std::shared_ptr<NetworkConditioner> conditioner;

    EventCollector<std::string> joined;
    EventCollector<std::string> left;
    EventCollector<std::string> streaming_started;
    EventCollector<std::string> streaming_stopped;
    EventCollector<std::string> watch_started;
    EventCollector<std::string> watch_stopped;

    Waiter channel_open;
    EventCollector<std::string> channel_open_events;
    EventCollector<ReceivedPacket> received;

    explicit PeerNode(std::string channel_label = "data")
        : transport(make_test_transport())
        , label(std::move(channel_label))
    {
        register_transport_callbacks_();
        register_channel_(/*conditioner=*/nullptr);
    }

    // Conditioned constructor: applies NetProfile impairments to the receive
    // path of this node's primary data channel. Must be called before
    // connect() so the wrapped callback is captured at DataChannel-open time.
    PeerNode(std::string channel_label, NetProfile profile)
        : transport(make_test_transport())
        , label(std::move(channel_label))
        , conditioner(std::make_shared<NetworkConditioner>(std::move(profile)))
    {
        register_transport_callbacks_();
        register_channel_(conditioner.get());
    }

private:
    void register_transport_callbacks_()
    {
        transport->on_peer_joined(
            [this](const std::string& id) { joined.push(id); });
        transport->on_peer_left(
            [this](const std::string& id) { left.push(id); });
        transport->on_streaming_started(
            [this](const std::string& id) { streaming_started.push(id); });
        transport->on_streaming_stopped(
            [this](const std::string& id) { streaming_stopped.push(id); });
        transport->on_watch_started(
            [this](const std::string& id) { watch_started.push(id); });
        transport->on_watch_stopped(
            [this](const std::string& id) { watch_stopped.push(id); });
    }

    // Registers the primary data channel. If cond is non-null the on_data
    // callback is wrapped through the conditioner before registration.
    void register_channel_(NetworkConditioner* cond)
    {
        Transport::ChannelSpec spec;
        spec.label = label;
        spec.unordered = false;
        spec.max_retransmits = -1;
        spec.on_open = [this](const std::string& peer) {
            channel_open.signal();
            channel_open_events.push(peer);
        };

        PacketCb real_on_data = [this](const std::string& peer,
                                    const uint8_t* data,
                                    size_t len) {
            received.push(ReceivedPacket {
                peer, std::vector<uint8_t>(data, data + len) });
        };

        if (cond) {
            spec.on_data = cond->wrap(std::move(real_on_data));
        } else {
            spec.on_data = std::move(real_on_data);
        }

        spec.on_close = [](const std::string&) { };
        transport->register_channel(std::move(spec));
    }

public:
    // Add a second (or third) channel with its own on_data collector.
    // Returns a shared pointer to the collector so the test can assert on
    // per-channel delivery.
    std::shared_ptr<EventCollector<ReceivedPacket>> add_channel(
        const std::string& extra_label)
    {
        auto collector = std::make_shared<EventCollector<ReceivedPacket>>();
        Transport::ChannelSpec spec;
        spec.label = extra_label;
        spec.unordered = false;
        spec.max_retransmits = -1;
        spec.on_open = [](const std::string&) { };
        spec.on_data = [collector](const std::string& peer,
                           const uint8_t* data,
                           size_t len) {
            collector->push(ReceivedPacket {
                peer, std::vector<uint8_t>(data, data + len) });
        };
        spec.on_close = [](const std::string&) { };
        transport->register_channel(std::move(spec));
        return collector;
    }

    bool connect(const std::string& ws_url)
    {
        auto r = transport->connect(ws_url);
        if (!r.has_value()) {
            return false;
        }
        return wait_for_local_id(*transport);
    }

    std::string id() const { return transport->local_id(); }
};

// Waits until both nodes have seen each other join AND each has the data
// channel open. Returns true if both conditions are met within the timeout.
inline bool wait_for_rendezvous(PeerNode& a,
    PeerNode& b,
    std::chrono::milliseconds timeout = kDefaultTimeout)
{
    if (!a.joined.wait_for_count(1, timeout)) {
        return false;
    }
    if (!b.joined.wait_for_count(1, timeout)) {
        return false;
    }
    if (!a.channel_open.wait_for(timeout)) {
        return false;
    }
    if (!b.channel_open.wait_for(timeout)) {
        return false;
    }
    return true;
}

} // namespace test_util
