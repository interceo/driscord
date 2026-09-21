#include "utils/proxy_config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

namespace utils {
namespace {

    constexpr std::uint16_t kDefaultHttpPort = 8080;
    constexpr std::uint16_t kDefaultSocksPort = 1080;

    std::string to_lower(std::string_view text)
    {
        std::string lowered(text);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    }

    std::string_view trim(std::string_view text)
    {
        const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!text.empty() && is_space(static_cast<unsigned char>(text.front()))) {
            text.remove_prefix(1);
        }
        while (!text.empty() && is_space(static_cast<unsigned char>(text.back()))) {
            text.remove_suffix(1);
        }
        return text;
    }

    std::optional<std::uint16_t> parse_port(std::string_view text)
    {
        if (text.empty() || text.size() > 5) {
            return std::nullopt;
        }
        unsigned value = 0;
        for (const char c : text) {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
                return std::nullopt;
            }
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
        if (value == 0 || value > 65535) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(value);
    }

    // Splits "host:port", "[v6]:port", "host" or "[v6]". The host keeps its
    // brackets off; the port is nullopt when absent.
    bool split_host_port(std::string_view authority, std::string& host,
        std::optional<std::uint16_t>& port)
    {
        if (authority.empty()) {
            return false;
        }
        if (authority.front() == '[') {
            const auto close = authority.find(']');
            if (close == std::string_view::npos || close == 1) {
                return false;
            }
            host = std::string(authority.substr(1, close - 1));
            const auto rest = authority.substr(close + 1);
            if (rest.empty()) {
                return true;
            }
            if (rest.front() != ':') {
                return false;
            }
            port = parse_port(rest.substr(1));
            return port.has_value();
        }

        const auto colon = authority.rfind(':');
        if (colon == std::string_view::npos) {
            host = std::string(authority);
            return !host.empty();
        }
        // A second colon without brackets is a bare IPv6 literal, which has no
        // room for a port.
        if (authority.find(':') != colon) {
            host = std::string(authority);
            return true;
        }
        host = std::string(authority.substr(0, colon));
        if (host.empty()) {
            return false;
        }
        port = parse_port(authority.substr(colon + 1));
        return port.has_value();
    }

    std::optional<std::string> environment_value(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr) {
            return std::nullopt;
        }
        const std::string_view trimmed = trim(value);
        if (trimmed.empty()) {
            return std::nullopt;
        }
        return std::string(trimmed);
    }

    // Upper spelling first, then lower; an empty value counts as unset so
    // `HTTPS_PROXY= driscord` can turn a shell-exported proxy off again.
    std::optional<std::string> proxy_variable(const char* upper, const char* lower)
    {
        if (auto value = environment_value(upper)) {
            return value;
        }
        return environment_value(lower);
    }

}

std::string ProxyConfig::url() const
{
    const std::string_view prefix
        = scheme == Scheme::Socks5 ? "socks5://" : "http://";
    const bool literal_v6 = host.find(':') != std::string::npos;
    std::string rendered(prefix);
    if (literal_v6) {
        rendered += '[';
        rendered += host;
        rendered += ']';
    } else {
        rendered += host;
    }
    rendered += ':';
    rendered += std::to_string(port);
    return rendered;
}

std::optional<ProxyConfig> parse_proxy_url(std::string_view text)
{
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }

    ProxyConfig proxy;
    const auto scheme_end = text.find("://");
    if (scheme_end != std::string_view::npos) {
        const std::string scheme = to_lower(text.substr(0, scheme_end));
        if (scheme == "http") {
            proxy.scheme = ProxyConfig::Scheme::Http;
        } else if (scheme == "socks5" || scheme == "socks5h") {
            proxy.scheme = ProxyConfig::Scheme::Socks5;
        } else {
            return std::nullopt;
        }
        text.remove_prefix(scheme_end + 3);
    }

    // Everything after the last '@' is the authority; credentials before it
    // are optional.
    if (const auto at = text.rfind('@'); at != std::string_view::npos) {
        const std::string_view credentials = text.substr(0, at);
        text.remove_prefix(at + 1);
        const auto colon = credentials.find(':');
        if (colon == std::string_view::npos) {
            proxy.username = std::string(credentials);
        } else {
            proxy.username = std::string(credentials.substr(0, colon));
            proxy.password = std::string(credentials.substr(colon + 1));
        }
        if (proxy.username.empty()) {
            return std::nullopt;
        }
    }

    // A trailing path is meaningless for a proxy endpoint but common in
    // copy-pasted values, so drop it instead of refusing.
    if (const auto slash = text.find('/'); slash != std::string_view::npos) {
        text = text.substr(0, slash);
    }

    std::optional<std::uint16_t> port;
    if (!split_host_port(text, proxy.host, port)) {
        return std::nullopt;
    }
    proxy.port = port.value_or(proxy.scheme == ProxyConfig::Scheme::Socks5
            ? kDefaultSocksPort
            : kDefaultHttpPort);
    return proxy;
}

bool no_proxy_matches(std::string_view no_proxy, std::string_view host)
{
    if (host.empty()) {
        return false;
    }
    const std::string target = to_lower(host);

    std::size_t position = 0;
    while (position <= no_proxy.size()) {
        auto end = no_proxy.find_first_of(", \t", position);
        if (end == std::string_view::npos) {
            end = no_proxy.size();
        }
        const std::string entry = to_lower(trim(no_proxy.substr(position, end - position)));
        position = end + 1;
        if (entry.empty()) {
            continue;
        }
        if (entry == "*") {
            return true;
        }
        std::string_view suffix = entry;
        bool subdomain_only = false;
        if (suffix.starts_with("*.")) {
            suffix.remove_prefix(2);
            subdomain_only = true;
        } else if (suffix.starts_with('.')) {
            suffix.remove_prefix(1);
            subdomain_only = true;
        }
        if (suffix.empty()) {
            continue;
        }
        if (!subdomain_only && target == suffix) {
            return true;
        }
        if (target.size() > suffix.size()
            && target.ends_with(suffix)
            && target[target.size() - suffix.size() - 1] == '.') {
            return true;
        }
    }
    return false;
}

std::optional<ProxyConfig> proxy_from_environment(std::string_view target_scheme,
    std::string_view target_host)
{
    if (const auto no_proxy = proxy_variable("NO_PROXY", "no_proxy")) {
        if (no_proxy_matches(*no_proxy, target_host)) {
            return std::nullopt;
        }
    }

    const std::string scheme = to_lower(target_scheme);
    const bool secure = scheme == "https" || scheme == "wss";

    using Names = std::array<const char*, 2>;
    std::array<Names, 3> candidates { };
    std::size_t count = 0;
    if (secure) {
        candidates[count++] = Names { "HTTPS_PROXY", "https_proxy" };
        candidates[count++] = Names { "ALL_PROXY", "all_proxy" };
        candidates[count++] = Names { "HTTP_PROXY", "http_proxy" };
    } else {
        candidates[count++] = Names { "HTTP_PROXY", "http_proxy" };
        candidates[count++] = Names { "ALL_PROXY", "all_proxy" };
    }

    for (std::size_t index = 0; index < count; ++index) {
        if (auto value = proxy_variable(candidates[index][0], candidates[index][1])) {
            if (auto proxy = parse_proxy_url(*value)) {
                return proxy;
            }
            // A malformed value is not silently replaced by the next variable:
            // that would route traffic somewhere the operator did not name.
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<ProxyConfig> proxy_for_url(std::string_view url)
{
    url = trim(url);
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string scheme = to_lower(url.substr(0, scheme_end));
    std::string_view authority = url.substr(scheme_end + 3);
    if (const auto slash = authority.find('/'); slash != std::string_view::npos) {
        authority = authority.substr(0, slash);
    }
    if (const auto at = authority.rfind('@'); at != std::string_view::npos) {
        authority.remove_prefix(at + 1);
    }
    std::string host;
    std::optional<std::uint16_t> port;
    if (!split_host_port(authority, host, port)) {
        return std::nullopt;
    }
    return proxy_from_environment(scheme, host);
}

}
