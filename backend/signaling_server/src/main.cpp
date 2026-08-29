#include <boost/asio.hpp>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <rtc/rtc.hpp>
#include <stdexcept>
#include <string>

#include "api_authenticator.hpp"
#include "log.hpp"
#include "ws_server.hpp"

namespace {

bool env_flag(const char* name)
{
    const char* value = std::getenv(name);
    return value
        && (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0);
}

unsigned short parse_port(const char* value)
{
    size_t consumed = 0;
    const auto parsed = std::stoul(value, &consumed);
    if (consumed != std::strlen(value) || parsed == 0
        || parsed > std::numeric_limits<unsigned short>::max()) {
        throw std::out_of_range("port must be between 1 and 65535");
    }
    return static_cast<unsigned short>(parsed);
}

std::shared_ptr<driscord::ApiAuthenticator> make_authenticator(
    boost::asio::io_context& io)
{
    const char* api_url = std::getenv("DRISCORD_API_URL");
    if (api_url && *api_url) {
        auto authenticator = driscord::ApiAuthenticator::create(io, api_url);
        if (!authenticator) {
            throw std::runtime_error(
                "DRISCORD_API_URL is set but unusable; refusing to start");
        }
        return authenticator;
    }
    if (!env_flag("DRISCORD_ALLOW_ANONYMOUS")) {
        throw std::runtime_error(
            "DRISCORD_API_URL is not set. Point it at the REST API so channel "
            "membership can be checked, or set DRISCORD_ALLOW_ANONYMOUS=1 to "
            "run an open server on purpose.");
    }
    return nullptr;
}

}

int main(int argc, char** argv)
{
    try {
        unsigned short port = 9001;
        if (const char* env = std::getenv("DRISCORD_PORT")) {
            port = parse_port(env);
        }
        if (argc > 1) {
            port = parse_port(argv[1]);
        }

        boost::asio::io_context io;
        auto server = std::make_shared<driscord::WebSocketServer>(
            io, port, driscord::sfu::RtpFaultConfig { },
            make_authenticator(io));
        server->run();

        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](boost::system::error_code, int sig) {
            LOG_INFO() << "received signal " << sig << ", shutting down";
            server->stop();
            io.stop();
        });

        LOG_INFO() << "driscord ws server listening on port " << port;
        io.run();
        LOG_INFO() << "server stopped";
    } catch (const std::exception& ex) {
        LOG_ERROR() << "fatal: " << ex.what();
        rtc::Cleanup().wait();
        return 1;
    }
    rtc::Cleanup().wait();
    return 0;
}
