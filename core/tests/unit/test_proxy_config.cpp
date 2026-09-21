#include "utils/proxy_config.hpp"

#include "proxy_environment.hpp"

#include <gtest/gtest.h>

namespace {

using test_util::ScopedProxyEnvironment;
using utils::ProxyConfig;

TEST(ProxyConfig, ParsesAnExplicitHttpEndpoint)
{
    const auto proxy = utils::parse_proxy_url("http://127.0.0.1:3128");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->scheme, ProxyConfig::Scheme::Http);
    EXPECT_EQ(proxy->host, "127.0.0.1");
    EXPECT_EQ(proxy->port, 3128);
    EXPECT_FALSE(proxy->authenticated());
}

TEST(ProxyConfig, AssumesHttpWhenTheSchemeIsMissing)
{
    const auto proxy = utils::parse_proxy_url("proxy.lan:8888");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->scheme, ProxyConfig::Scheme::Http);
    EXPECT_EQ(proxy->host, "proxy.lan");
    EXPECT_EQ(proxy->port, 8888);
}

TEST(ProxyConfig, FillsInTheDefaultPortPerScheme)
{
    const auto http = utils::parse_proxy_url("http://proxy.lan");
    ASSERT_TRUE(http.has_value());
    EXPECT_EQ(http->port, 8080);

    const auto socks = utils::parse_proxy_url("socks5://proxy.lan");
    ASSERT_TRUE(socks.has_value());
    EXPECT_EQ(socks->scheme, ProxyConfig::Scheme::Socks5);
    EXPECT_EQ(socks->port, 1080);
}

TEST(ProxyConfig, KeepsCredentials)
{
    const auto proxy = utils::parse_proxy_url("socks5://bob:s3cret@10.0.0.2:1080");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->username, "bob");
    EXPECT_EQ(proxy->password, "s3cret");
    EXPECT_TRUE(proxy->authenticated());
    // Logging must not leak the password.
    EXPECT_EQ(proxy->url(), "socks5://10.0.0.2:1080");
}

TEST(ProxyConfig, HandlesBracketedIpv6)
{
    const auto proxy = utils::parse_proxy_url("http://[::1]:3128");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "::1");
    EXPECT_EQ(proxy->port, 3128);
    EXPECT_EQ(proxy->url(), "http://[::1]:3128");
}

TEST(ProxyConfig, RejectsATlsProxyEndpointInsteadOfDowngradingIt)
{
    EXPECT_FALSE(utils::parse_proxy_url("https://proxy.lan:3128").has_value());
}

TEST(ProxyConfig, RejectsGarbage)
{
    EXPECT_FALSE(utils::parse_proxy_url("").has_value());
    EXPECT_FALSE(utils::parse_proxy_url("   ").has_value());
    EXPECT_FALSE(utils::parse_proxy_url("http://host:0").has_value());
    EXPECT_FALSE(utils::parse_proxy_url("http://host:70000").has_value());
    EXPECT_FALSE(utils::parse_proxy_url("http://host:port").has_value());
    EXPECT_FALSE(utils::parse_proxy_url("ftp://host:21").has_value());
}

TEST(ProxyConfig, DropsATrailingPath)
{
    const auto proxy = utils::parse_proxy_url("http://proxy.lan:3128/");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "proxy.lan");
    EXPECT_EQ(proxy->port, 3128);
}

TEST(NoProxy, MatchesExactHostsAndSubdomains)
{
    EXPECT_TRUE(utils::no_proxy_matches("localhost", "localhost"));
    EXPECT_TRUE(utils::no_proxy_matches("LOCALHOST", "localhost"));
    EXPECT_FALSE(utils::no_proxy_matches("localhost", "notlocalhost"));

    EXPECT_TRUE(utils::no_proxy_matches(".homelab.lan", "driscord.homelab.lan"));
    EXPECT_TRUE(utils::no_proxy_matches("*.homelab.lan", "driscord.homelab.lan"));
    EXPECT_TRUE(utils::no_proxy_matches("homelab.lan", "driscord.homelab.lan"));
    EXPECT_FALSE(utils::no_proxy_matches(".homelab.lan", "homelab.lan.evil.tld"));
}

TEST(NoProxy, StarMatchesEverything)
{
    EXPECT_TRUE(utils::no_proxy_matches("*", "anything.example"));
}

TEST(NoProxy, SplitsOnCommasAndWhitespace)
{
    EXPECT_TRUE(utils::no_proxy_matches("a.tld, b.tld  c.tld", "b.tld"));
    EXPECT_TRUE(utils::no_proxy_matches("a.tld, b.tld  c.tld", "c.tld"));
    EXPECT_FALSE(utils::no_proxy_matches("a.tld, b.tld  c.tld", "d.tld"));
}

TEST(ProxyEnvironmentLookup, PrefersTheSchemeSpecificVariable)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("HTTPS_PROXY", "http://secure.lan:3128");
    ScopedProxyEnvironment::set("HTTP_PROXY", "http://plain.lan:3128");

    const auto proxy = utils::proxy_from_environment("wss", "driscord.tld");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "secure.lan");
}

TEST(ProxyEnvironmentLookup, FallsBackToHttpProxyForTlsTargets)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("HTTP_PROXY", "http://plain.lan:3128");

    const auto proxy = utils::proxy_from_environment("https", "driscord.tld");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "plain.lan");
}

TEST(ProxyEnvironmentLookup, PlainWsUsesHttpProxy)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("HTTP_PROXY", "http://plain.lan:3128");

    const auto proxy = utils::proxy_from_environment("ws", "127.0.0.1");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "plain.lan");
}

TEST(ProxyEnvironmentLookup, AllProxyOutranksTheOppositeScheme)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("ALL_PROXY", "socks5://all.lan:1080");
    ScopedProxyEnvironment::set("HTTP_PROXY", "http://plain.lan:3128");

    const auto proxy = utils::proxy_from_environment("wss", "driscord.tld");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "all.lan");
}

TEST(ProxyEnvironmentLookup, AcceptsTheLowercaseSpelling)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("https_proxy", "http://lower.lan:3128");

    const auto proxy = utils::proxy_from_environment("https", "driscord.tld");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "lower.lan");
}

TEST(ProxyEnvironmentLookup, AnEmptyValueCountsAsUnset)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("HTTPS_PROXY", "");
    ScopedProxyEnvironment::set("HTTP_PROXY", "http://plain.lan:3128");

    const auto proxy = utils::proxy_from_environment("https", "driscord.tld");
    ASSERT_TRUE(proxy.has_value());
    EXPECT_EQ(proxy->host, "plain.lan");
}

TEST(ProxyEnvironmentLookup, NoProxyWins)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("HTTPS_PROXY", "http://secure.lan:3128");
    ScopedProxyEnvironment::set("NO_PROXY", ".homelab.lan");

    EXPECT_FALSE(
        utils::proxy_from_environment("wss", "driscord.homelab.lan").has_value());
    EXPECT_TRUE(
        utils::proxy_from_environment("wss", "driscord.example").has_value());
}

TEST(ProxyEnvironmentLookup, AMalformedValueDisablesTheProxyRatherThanFallingThrough)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("HTTPS_PROXY", "http://secure.lan:notaport");
    ScopedProxyEnvironment::set("HTTP_PROXY", "http://plain.lan:3128");

    EXPECT_FALSE(
        utils::proxy_from_environment("https", "driscord.tld").has_value());
}

TEST(ProxyForUrl, TakesTheSchemeAndHostFromTheUrl)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("HTTPS_PROXY", "http://secure.lan:3128");
    ScopedProxyEnvironment::set("NO_PROXY", "driscord.homelab.lan");

    EXPECT_TRUE(utils::proxy_for_url("wss://other.tld/ws").has_value());
    EXPECT_FALSE(
        utils::proxy_for_url("wss://driscord.homelab.lan:443/ws").has_value());
}

TEST(ProxyForUrl, RejectsAUrlWithoutAScheme)
{
    ScopedProxyEnvironment guard;
    ScopedProxyEnvironment::set("ALL_PROXY", "http://secure.lan:3128");

    EXPECT_FALSE(utils::proxy_for_url("driscord.tld/ws").has_value());
}

}
