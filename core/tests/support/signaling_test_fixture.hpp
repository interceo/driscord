#pragma once

#include "api_authenticator.hpp"
#include "ws_server.hpp"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace test_util {

// Spins up an in-process driscord::WebSocketServer on an OS-assigned
// ephemeral port and runs its io_context on a background thread. The
// fixture's destructor stops the server cleanly so RAII order in test
// bodies (declare fixture first → destruct last) avoids races with
// Transport's WS callbacks.
class SignalingServerFixture {
public:
    // `api_base_url` turns on session authorization against a REST API; empty
    // keeps the server anonymous, which is what the media/room tests want.
    explicit SignalingServerFixture(
        driscord::sfu::RtpFaultConfig fault_config = { },
        const std::string& api_base_url = { })
    {
        // Tests talk over loopback, where host candidates always suffice and
        // a STUN lookup can only hurt: on the offline CI runner the juice
        // resolver blocks in getaddrinfo for the glibc DNS timeout, and
        // libdatachannel's global teardown thread then joins that resolver in
        // ~IceTransport, wedging every queued transport teardown behind it
        // (which is how disconnect tests miss their server-noticed-disconnect
        // deadlines). An explicit environment value still wins.
        if (std::getenv("DRISCORD_ICE_STUN_URLS") == nullptr) {
#ifdef _WIN32
            ::_putenv_s("DRISCORD_ICE_STUN_URLS", "none");
#else
            ::setenv("DRISCORD_ICE_STUN_URLS", "none", /*overwrite=*/1);
#endif
        }
        // Opt-in libdatachannel tracing for flake hunts: the signaling
        // reconnect races live below Transport, where our own logs are blind.
        static const bool rtc_log_enabled = [] {
            const char* level = std::getenv("DRISCORD_TEST_RTC_LOG");
            if (level == nullptr) {
                return false;
            }
            rtc::InitLogger(std::string_view(level) == "verbose"
                    ? rtc::LogLevel::Verbose
                    : rtc::LogLevel::Debug);
            return true;
        }();
        (void)rtc_log_enabled;
        std::shared_ptr<driscord::ApiAuthenticator> authenticator;
        if (!api_base_url.empty()) {
            authenticator
                = driscord::ApiAuthenticator::create(io_, api_base_url);
        }
        server_ = std::make_shared<driscord::WebSocketServer>(
            io_, /*port=*/0, fault_config, std::move(authenticator));
        server_->run();
        port_ = server_->bound_port();
        work_.emplace(boost::asio::make_work_guard(io_));
        thread_ = std::thread([this] { io_.run(); });
    }

    ~SignalingServerFixture()
    {
        if (server_) {
            server_->stop();
        }
        work_.reset();
        io_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    SignalingServerFixture(const SignalingServerFixture&) = delete;
    SignalingServerFixture& operator=(const SignalingServerFixture&) = delete;

    std::string ws_url() const
    {
        return "ws://127.0.0.1:" + std::to_string(port_);
    }

    // Returns a URL that joins a specific voice channel room, matching
    // the path the Kotlin client sends: /channels/{id}
    std::string ws_url(int channel_id) const
    {
        return "ws://127.0.0.1:" + std::to_string(port_)
            + "/channels/" + std::to_string(channel_id);
    }

    unsigned short port() const { return port_; }

    // Blocking plain-HTTP GET against the server's own listener, for the
    // read-only endpoints it serves next to the WebSocket upgrade.
    std::pair<unsigned, std::string> http_get(const std::string& target,
        const std::string& token = { }) const
    {
        namespace beast = boost::beast;
        namespace http = beast::http;
        using tcp = boost::asio::ip::tcp;

        boost::asio::io_context io;
        tcp::socket socket(io);
        socket.connect(tcp::endpoint(
            boost::asio::ip::make_address("127.0.0.1"), port_));

        http::request<http::empty_body> request { http::verb::get, target, 11 };
        request.set(http::field::host, "127.0.0.1");
        if (!token.empty()) {
            request.set(http::field::authorization, "Bearer " + token);
        }
        http::write(socket, request);

        beast::flat_buffer buffer;
        http::response<http::string_body> response;
        boost::system::error_code ec;
        http::read(socket, buffer, response, ec);
        if (ec && ec != http::error::end_of_stream) {
            return { 0, ec.message() };
        }
        return { response.result_int(), response.body() };
    }

    size_t active_sessions() const { return server_->active_sessions(); }
    size_t active_sessions(const std::string& room_id) const
    {
        return server_->active_sessions(driscord::RoomId { room_id });
    }

private:
    boost::asio::io_context io_;
    std::shared_ptr<driscord::WebSocketServer> server_;
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;
    std::thread thread_;
    unsigned short port_ { 0 };
};

} // namespace test_util
