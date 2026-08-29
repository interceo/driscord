#pragma once

#include <boost/asio/io_context.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace driscord {

class ApiAuthenticator : public std::enable_shared_from_this<ApiAuthenticator> {
public:
    struct Identity {
        std::string username;
        long long user_id = 0;
    };

    using Callback = std::function<void(std::optional<Identity>)>;

    static std::shared_ptr<ApiAuthenticator> create(
        boost::asio::io_context& io_context, const std::string& base_url);

    ApiAuthenticator(boost::asio::io_context& io_context,
        std::string host,
        std::string port);

    void authorize_channel(std::string token,
        std::string channel,
        Callback callback);

    void authorize_user(std::string token, Callback callback);

    const std::string& host() const noexcept { return host_; }
    const std::string& port() const noexcept { return port_; }

private:
    void request(std::string target, std::string token, Callback callback);

    boost::asio::io_context& io_context_;
    const std::string host_;
    const std::string port_;
};

}
