#include "api_authenticator.hpp"

#include "log.hpp"

#include <algorithm>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <cctype>
#include <charconv>
#include <chrono>
#include <nlohmann/json.hpp>
#include <utility>

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

namespace driscord {
namespace {

    constexpr auto kRequestTimeout = std::chrono::seconds(5);

    std::string url_escape(std::string_view value)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(value.size());
        for (const unsigned char c : value) {
            const bool unreserved = (c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '-' || c == '.' || c == '_' || c == '~';
            if (unreserved) {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back('%');
                out.push_back(kHex[c >> 4]);
                out.push_back(kHex[c & 0x0f]);
            }
        }
        return out;
    }

    class Call : public std::enable_shared_from_this<Call> {
    public:
        Call(boost::asio::io_context& io_context,
            std::string host,
            std::string port,
            std::string target,
            std::string token,
            ApiAuthenticator::Callback callback)
            : resolver_(io_context)
            , stream_(io_context)
            , host_(std::move(host))
            , port_(std::move(port))
            , callback_(std::move(callback))
        {
            request_.version(11);
            request_.method(http::verb::get);
            request_.target(target);
            request_.set(http::field::host,
                port_ == "80" ? host_ : host_ + ":" + port_);
            request_.set(http::field::user_agent, "driscord-sfu");
            if (!token.empty()) {
                request_.set(http::field::authorization, "Bearer " + token);
            }
        }

        void run()
        {
            resolver_.async_resolve(host_, port_,
                beast::bind_front_handler(&Call::on_resolve, shared_from_this()));
        }

    private:
        void fail(beast::error_code ec, const char* what)
        {
            LOG_WARNING() << "session authorization " << what << ": "
                          << ec.message();
            finish(std::nullopt);
        }

        void finish(std::optional<ApiAuthenticator::Identity> identity)
        {
            if (auto callback = std::exchange(callback_, nullptr)) {
                callback(std::move(identity));
            }
            beast::error_code ignored;
            (void)stream_.socket().shutdown(
                tcp::socket::shutdown_both, ignored);
        }

        void on_resolve(beast::error_code ec, tcp::resolver::results_type results)
        {
            if (ec) {
                return fail(ec, "resolve failed");
            }
            stream_.expires_after(kRequestTimeout);
            stream_.async_connect(results,
                beast::bind_front_handler(&Call::on_connect, shared_from_this()));
        }

        void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
        {
            if (ec) {
                return fail(ec, "connect failed");
            }
            stream_.expires_after(kRequestTimeout);
            http::async_write(stream_, request_,
                beast::bind_front_handler(&Call::on_write, shared_from_this()));
        }

        void on_write(beast::error_code ec, std::size_t)
        {
            if (ec) {
                return fail(ec, "write failed");
            }
            http::async_read(stream_, buffer_, response_,
                beast::bind_front_handler(&Call::on_read, shared_from_this()));
        }

        void on_read(beast::error_code ec, std::size_t)
        {
            if (ec) {
                return fail(ec, "read failed");
            }
            if (response_.result() != http::status::ok) {
                LOG_INFO() << "session authorization refused by API: "
                           << response_.result_int();
                return finish(std::nullopt);
            }
            try {
                const auto body = nlohmann::json::parse(response_.body());
                ApiAuthenticator::Identity identity;
                if (body.contains("username")
                    && body["username"].is_string()) {
                    identity.username = body["username"].get<std::string>();
                }
                const auto id = body.contains("user_id") ? body.find("user_id")
                                                         : body.find("id");
                if (id != body.end() && id->is_number_integer()) {
                    identity.user_id = id->get<long long>();
                }
                if (identity.username.empty()) {
                    LOG_WARNING() << "API authorization response has no username";
                    return finish(std::nullopt);
                }
                finish(std::move(identity));
            } catch (const std::exception& error) {
                LOG_WARNING() << "API authorization response is not usable: "
                              << error.what();
                finish(std::nullopt);
            }
        }

        tcp::resolver resolver_;
        beast::tcp_stream stream_;
        const std::string host_;
        const std::string port_;
        http::request<http::empty_body> request_;
        http::response<http::string_body> response_;
        beast::flat_buffer buffer_;
        ApiAuthenticator::Callback callback_;
    };

}

std::shared_ptr<ApiAuthenticator> ApiAuthenticator::create(
    boost::asio::io_context& io_context, const std::string& base_url)
{
    std::string_view rest = base_url;
    constexpr std::string_view kScheme = "http://";
    if (!rest.starts_with(kScheme)) {
        LOG_ERROR() << "DRISCORD_API_URL must start with http://; the API is "
                       "expected on a trusted internal network";
        return nullptr;
    }
    rest.remove_prefix(kScheme.size());

    const auto suffixStart = rest.find_first_of("/?#");
    const auto authority = rest.substr(0, suffixStart);
    if (suffixStart != std::string_view::npos
        && rest.substr(suffixStart).find_first_not_of('/')
            != std::string_view::npos) {
        LOG_ERROR() << "DRISCORD_API_URL must not contain a path, query, or "
                       "fragment: "
                    << base_url;
        return nullptr;
    }
    if (authority.empty()) {
        LOG_ERROR() << "DRISCORD_API_URL has no host";
        return nullptr;
    }

    std::string host(authority);
    std::string port = "80";
    const auto colon = host.rfind(':');
    if (colon != std::string::npos) {
        if (host.find(':') != colon) {
            LOG_ERROR() << "DRISCORD_API_URL does not support IPv6 literals: "
                        << base_url;
            return nullptr;
        }
        port = host.substr(colon + 1);
        host = host.substr(0, colon);
    }
    const auto invalidHost = std::find_if(host.cbegin(), host.cend(), [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return std::isspace(byte) != 0 || std::iscntrl(byte) != 0 || c == '@'
            || c == '\\' || c == '[' || c == ']';
    });
    unsigned int numericPort = 0;
    const auto [portEnd, portError]
        = std::from_chars(port.data(), port.data() + port.size(), numericPort);
    if (host.empty() || invalidHost != host.cend() || port.empty()
        || portError != std::errc { } || portEnd != port.data() + port.size()
        || numericPort == 0 || numericPort > 65535) {
        LOG_ERROR() << "DRISCORD_API_URL is malformed: " << base_url;
        return nullptr;
    }
    return std::make_shared<ApiAuthenticator>(
        io_context, std::move(host), std::move(port));
}

ApiAuthenticator::ApiAuthenticator(boost::asio::io_context& io_context,
    std::string host,
    std::string port)
    : io_context_(io_context)
    , host_(std::move(host))
    , port_(std::move(port))
{
}

void ApiAuthenticator::authorize_channel(std::string token,
    std::string channel,
    Callback callback)
{
    request("/channels/" + url_escape(channel) + "/access", std::move(token),
        std::move(callback));
}

void ApiAuthenticator::authorize_user(std::string token, Callback callback)
{
    request("/users/me", std::move(token), std::move(callback));
}

void ApiAuthenticator::request(std::string target,
    std::string token,
    Callback callback)
{
    if (!callback) {
        return;
    }
    if (token.empty()) {
        callback(std::nullopt);
        return;
    }
    std::make_shared<Call>(io_context_, host_, port_, std::move(target),
        std::move(token), std::move(callback))
        ->run();
}

}
