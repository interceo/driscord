#include <boost/asio.hpp>
#include <cstdlib>

#include "log.hpp"
#include "ws_server.hpp"

int main(int argc, char** argv) {
    try {
        unsigned short port = 8080;
        if (const char* env = std::getenv("DRISCORD_PORT")) {
            port = static_cast<unsigned short>(std::stoi(env));
        }
        if (argc > 1) {
            port = static_cast<unsigned short>(std::stoi(argv[1]));
        }

        boost::asio::io_context io;
        auto server = std::make_shared<driscord::WebSocketServer>(io, port);
        server->run();

        LOG_INFO() << "driscord ws server listening on port " << port;
        io.run();
    } catch (const std::exception& ex) {
        LOG_ERROR() << "fatal: " << ex.what();
        return 1;
    }
    return 0;
}
