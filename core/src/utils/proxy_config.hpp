#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace utils {

// One proxy endpoint, already split out of a URL.
struct ProxyConfig {
    enum class Scheme { Http,
        Socks5 };

    Scheme scheme = Scheme::Http;
    std::string host;
    std::uint16_t port = 0;
    std::string username;
    std::string password;

    [[nodiscard]] bool authenticated() const noexcept
    {
        return !username.empty();
    }

    // "http://host:3128" — credentials are never rendered, the result is
    // meant for logs.
    [[nodiscard]] std::string url() const;

    friend bool operator==(const ProxyConfig&, const ProxyConfig&) = default;
};

// Accepts "http://host:3128", "socks5://user:pw@host:1080", "[::1]:3128" and
// a bare "host:3128". A missing scheme means http; a missing port means 8080
// for http and 1080 for socks5. "https://" is rejected rather than downgraded:
// neither Qt's HttpProxy nor libdatachannel speaks TLS to the proxy itself, so
// accepting it would silently send plaintext where TLS was asked for.
std::optional<ProxyConfig> parse_proxy_url(std::string_view text);

// The NO_PROXY list: comma- or space-separated entries, "*" matches
// everything, a leading "." or "*." matches subdomains, everything else is an
// exact host match. Matching ignores case.
bool no_proxy_matches(std::string_view no_proxy, std::string_view host);

// Reads the proxy that applies to one target from the environment.
//
// For an https/wss target the order is HTTPS_PROXY, ALL_PROXY, HTTP_PROXY; for
// http/ws it is HTTP_PROXY, ALL_PROXY. The lowercase spelling of each name is
// consulted when the uppercase one is unset. Falling back to HTTP_PROXY for a
// TLS target is deliberate and differs from curl: every Driscord endpoint is
// https/wss, so honouring only HTTPS_PROXY would make the documented
// `HTTP_PROXY=… driscord` invocation do nothing at all.
//
// NO_PROXY is honoured in every case and wins over all of them.
std::optional<ProxyConfig> proxy_from_environment(std::string_view target_scheme,
    std::string_view target_host);

// proxy_from_environment() for a full URL; returns nothing when the URL has no
// host to match NO_PROXY against.
std::optional<ProxyConfig> proxy_for_url(std::string_view url);

}
