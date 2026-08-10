#pragma once

#include <boost/asio/io_context.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace driscord {

// Asks the REST API whether a bearer token may join a channel.
//
// The SFU has no user database and deliberately does not verify JWT signatures
// itself: that would put a second copy of the token format (and the signing
// secret) into a process that shares its address space with two TLS stacks.
// One HTTP call per WebSocket handshake answers both questions that matter —
// is the token valid, and is its owner a member of the channel's server.
class ApiAuthenticator : public std::enable_shared_from_this<ApiAuthenticator> {
public:
    struct Identity {
        std::string username;
        long long user_id = 0;
    };

    using Callback = std::function<void(std::optional<Identity>)>;

    // `base_url` is "http://host[:port]"; https is rejected because the API is
    // expected to sit on the deployment's internal network.
    static std::shared_ptr<ApiAuthenticator> create(
        boost::asio::io_context& io_context, const std::string& base_url);

    ApiAuthenticator(boost::asio::io_context& io_context,
        std::string host,
        std::string port);

    // GET /channels/{channel}/access — membership of the channel's server.
    void authorize_channel(std::string token,
        std::string channel,
        Callback callback);

    // GET /users/me — any valid token, for the read-only HTTP endpoints.
    void authorize_user(std::string token, Callback callback);

    const std::string& host() const noexcept { return host_; }
    const std::string& port() const noexcept { return port_; }

private:
    void request(std::string target, std::string token, Callback callback);

    boost::asio::io_context& io_context_;
    const std::string host_;
    const std::string port_;
};

} // namespace driscord
