#pragma once

#include "transport.hpp"
#include "wait_helpers.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace test_util {

inline bool wait_for_local_id(Transport& transport,
    std::chrono::milliseconds timeout = kDefaultTimeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!transport.local_id().empty()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return !transport.local_id().empty();
}

inline std::unique_ptr<Transport> make_test_transport()
{
    return std::make_unique<Transport>();
}

// Signaling-only peer used by roster/room tests. Media is covered separately
// by the real Google WebRTC ↔ SFU integration tests.
struct PeerNode {
    std::unique_ptr<Transport> transport = make_test_transport();
    EventCollector<std::string> joined;
    EventCollector<std::string> left;
    EventCollector<std::string> streaming_started;
    EventCollector<std::string> streaming_stopped;

    PeerNode()
    {
        transport->on_peer_joined(
            [this](const std::string& id) { joined.push(id); });
        transport->on_peer_left(
            [this](const std::string& id) { left.push(id); });
        transport->on_streaming_started(
            [this](const std::string& id) { streaming_started.push(id); });
        transport->on_streaming_stopped(
            [this](const std::string& id) { streaming_stopped.push(id); });
    }

    ~PeerNode()
    {
        if (transport) {
            transport->disconnect();
        }
    }

    PeerNode(const PeerNode&) = delete;
    PeerNode& operator=(const PeerNode&) = delete;

    bool connect(const std::string& ws_url)
    {
        return transport->connect(ws_url).has_value()
            && wait_for_local_id(*transport);
    }

    [[nodiscard]] std::string id() const { return transport->local_id(); }
};

inline bool wait_for_rendezvous(PeerNode& first,
    PeerNode& second,
    std::chrono::milliseconds timeout = kDefaultTimeout)
{
    return first.joined.wait_for_count(1, timeout)
        && second.joined.wait_for_count(1, timeout);
}

} // namespace test_util
