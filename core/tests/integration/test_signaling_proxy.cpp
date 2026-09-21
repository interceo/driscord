#include "http_connect_proxy.hpp"
#include "proxy_environment.hpp"
#include "signaling_test_fixture.hpp"
#include "transport.hpp"
#include "transport_harness.hpp"
#include "utils/log.hpp"

#include <gtest/gtest.h>

#include <string>

using test_util::HttpConnectProxy;
using test_util::PeerNode;
using test_util::ScopedProxyEnvironment;
using test_util::SignalingServerFixture;

namespace {

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
const SuppressLogs suppress_logs_on_startup;

std::string endpoint(unsigned short port)
{
    return "http://127.0.0.1:" + std::to_string(port);
}

TEST(SignalingProxy, ConnectsThroughAnHttpConnectProxy)
{
    SignalingServerFixture server;
    HttpConnectProxy proxy;

    ScopedProxyEnvironment environment;
    ScopedProxyEnvironment::set("HTTP_PROXY", endpoint(proxy.port()));

    PeerNode peer;
    ASSERT_TRUE(peer.connect(server.ws_url(1)));

    // A working connection is not evidence on its own: without the proxy it
    // would work too, straight to the server. The proxy having been asked for
    // this exact host and port is what proves the traffic went through it.
    const auto targets = proxy.targets();
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets.front(), "127.0.0.1:" + std::to_string(server.port()));
}

TEST(SignalingProxy, NoProxyExemptsTheSignalingHost)
{
    SignalingServerFixture server;
    HttpConnectProxy proxy;

    ScopedProxyEnvironment environment;
    ScopedProxyEnvironment::set("HTTP_PROXY", endpoint(proxy.port()));
    ScopedProxyEnvironment::set("NO_PROXY", "127.0.0.1");

    PeerNode peer;
    ASSERT_TRUE(peer.connect(server.ws_url(1)));

    EXPECT_TRUE(proxy.targets().empty());
}

TEST(SignalingProxy, RefusesToBypassAProxyItCannotSpeak)
{
    SignalingServerFixture server;

    ScopedProxyEnvironment environment;
    // Socks5 is a proxy libdatachannel has no support for. Connecting anyway
    // would leak the real address, which is exactly what someone testing from
    // a specific egress does not want, so the connection has to fail instead.
    ScopedProxyEnvironment::set("HTTP_PROXY", "socks5://127.0.0.1:1080");

    Transport transport;
    EXPECT_FALSE(transport.connect(server.ws_url(1)).has_value());
}

}
