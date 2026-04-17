#pragma once

#include "ws_server.hpp"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace test_util {

// Spins up an in-process driscord::WebSocketServer on an OS-assigned
// ephemeral port and runs its io_context on a background thread. The
// fixture's destructor stops the server cleanly so RAII order in test
// bodies (declare fixture first → destruct last) avoids races with
// Transport's WS callbacks.
class SignalingServerFixture {
public:
    SignalingServerFixture()
    {
        server_ = std::make_shared<driscord::WebSocketServer>(io_, /*port=*/0);
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

    size_t active_sessions() const { return server_->active_sessions(); }
    size_t active_sessions(const std::string& room_id) const
    {
        return server_->active_sessions(room_id);
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
