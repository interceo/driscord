#pragma once

#include "channel_labels.hpp"
#include "identity.hpp"

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <rtc/rtc.hpp>
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
    std::string register_and_build_welcome(const driscord::PeerId& id,
        const driscord::RoomId& room_id,
        std::shared_ptr<Session> s);

    void unregister_session(const driscord::PeerId& id,
        const driscord::RoomId& room_id);

    // Broadcast to all sessions in the same room except the sender.
    void broadcast(const driscord::PeerId& from_id,
        const driscord::RoomId& room_id,
        const std::string& msg);

    // Unicast: routes only within the given room.
    void send_to(const driscord::PeerId& target_id,
        const driscord::RoomId& room_id,
        const std::string& msg);

    // Media fan-out (SFU). Called from a client's DataChannel with the label
    // it arrived on; the server forwards the payload to the other sessions in
    // the room on their channel of the same label, prefixed with the sender id.
    // No codec-level decode happens here — the server only routes.
    void route_media(const driscord::PeerId& from_id,
        const driscord::RoomId& room_id,
        channel::MediaChannel label,
        const uint8_t* data,
        size_t len);

    // ICE configuration handed to every per-session PeerConnection.
    const rtc::Configuration& rtc_config() const { return rtc_config_; }

    // Total connected sessions across all rooms (for tests).
    size_t active_sessions() const;
    // Sessions in a specific room (for tests).
    size_t active_sessions(const driscord::RoomId& room_id) const;

    void add_streaming_peer(const driscord::PeerId& id,
        const driscord::RoomId& room_id);
    void remove_streaming_peer(const driscord::PeerId& id,
        const driscord::RoomId& room_id);

    // Watchers gate relayed screen video/screen-audio: only peers that sent
    // watch_start (and haven't sent watch_stop since) receive those relay
    // kinds, matching the P2P-mesh path's unicast-to-subscribers behavior.
    void add_video_watcher(const driscord::PeerId& id,
        const driscord::RoomId& room_id);
    void remove_video_watcher(const driscord::PeerId& id,
        const driscord::RoomId& room_id);

    // Snapshot of all rooms and their sessions for the /presence HTTP endpoint.
    // Returns a JSON string: { "<room_id>": [ { "id": "...", "username": "..." }, ... ], ... }
    std::string presence_json() const;
    std::string media_stats_json() const;

private:
    void do_accept();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    // Set by stop() so the accept loop stops re-arming itself. The acceptor is
    // not thread-safe, and do_accept() re-arms it from the io_context thread,
    // so closing it directly from a caller on another thread is a data race.
    std::atomic<bool> stopping_ { false };

    struct Room {
        std::unordered_map<driscord::PeerId, std::shared_ptr<Session>> sessions;
        std::unordered_set<driscord::PeerId> streaming_peers;
        std::unordered_set<driscord::PeerId> video_watchers;
        uint64_t media_packets_in = 0;
        uint64_t media_packets_out = 0;
        uint64_t media_bytes_in = 0;
        uint64_t media_bytes_out = 0;
        uint64_t media_packets_dropped = 0;
    };

    mutable std::mutex rooms_mutex_;
    std::unordered_map<driscord::RoomId, Room> rooms_;

    // Built once at construction. The UDP port range ICE binds to is taken
    // from DRISCORD_ICE_PORT_MIN/MAX so deployments behind NAT or in Docker
    // can pin and forward a known range.
    rtc::Configuration rtc_config_;
};

} // namespace driscord
