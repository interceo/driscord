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

    // Atomically inserts the new session into its room, snapshots existing
    // peers in that room (for the welcome message), and broadcasts peer_joined
    // to every pre-existing session in the same room. Returns the JSON-encoded
    // welcome payload. The welcome send itself happens OUTSIDE the critical
    // section.
    std::string register_and_build_welcome(const std::string& id,
        const std::string& room_id,
        std::shared_ptr<Session> s);

    void unregister_session(const std::string& id, const std::string& room_id);

    // Broadcast to all sessions in the same room except the sender.
    void broadcast(const std::string& from_id,
        const std::string& room_id,
        const std::string& msg);

    // Unicast: routes only within the given room.
    void send_to(const std::string& target_id,
        const std::string& room_id,
        const std::string& msg);

    // Total connected sessions across all rooms (for tests).
    size_t active_sessions() const;
    // Sessions in a specific room (for tests).
    size_t active_sessions(const std::string& room_id) const;

    void add_streaming_peer(const std::string& id, const std::string& room_id);
    void remove_streaming_peer(const std::string& id,
        const std::string& room_id);

    // Snapshot of all rooms and their sessions for the /presence HTTP endpoint.
    // Returns a JSON string: { "<room_id>": [ { "id": "...", "username": "..." }, ... ], ... }
    std::string presence_json() const;

private:
    void do_accept();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;

    struct Room {
        std::unordered_map<std::string, std::shared_ptr<Session>> sessions;
        std::unordered_set<std::string> streaming_peers;
    };

    mutable std::mutex rooms_mutex_;
    std::unordered_map<std::string, Room> rooms_;
};

} // namespace driscord
