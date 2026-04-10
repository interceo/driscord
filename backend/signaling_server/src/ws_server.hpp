#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace driscord {

class Session;

class WebSocketServer : public std::enable_shared_from_this<WebSocketServer> {
public:
    explicit WebSocketServer(boost::asio::io_context& io_context,
        unsigned short port);

    void run();
    void stop();

    unsigned short bound_port() const;

    // Atomically inserts the new session into the registry, snapshots the
    // existing peers (for the welcome message) and broadcasts peer_joined to
    // every pre-existing session. Returns the JSON-encoded welcome payload
    // that the caller must send to the newly registered session. The welcome
    // send itself happens OUTSIDE the critical section (Beast's async_write
    // is posted on the session's own strand), so multiple concurrent joiners
    // cannot miss each other.
    std::string register_and_build_welcome(const std::string& id,
        std::shared_ptr<Session> s);

    void unregister_session(const std::string& id);
    void broadcast(const std::string& from_id, const std::string& msg);
    void send_to(const std::string& target_id, const std::string& msg);

    // Returns the current number of connected sessions. Useful for tests
    // that want to assert cleanup after disconnect.
    size_t active_sessions() const;

    void add_streaming_peer(const std::string& id);
    void remove_streaming_peer(const std::string& id);

private:
    void do_accept();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;

    mutable std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
    std::unordered_set<std::string> streaming_peers_;
};

} // namespace driscord
