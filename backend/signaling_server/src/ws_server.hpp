#pragma once

#include "identity.hpp"
#include "sfu_media_utils.hpp"

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <rtc/rtc.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace driscord {

class ApiAuthenticator;
class Session;
class ScreenRouter;
class VoiceRouter;

class WebSocketServer : public std::enable_shared_from_this<WebSocketServer> {
public:
    explicit WebSocketServer(boost::asio::io_context& io_context,
        unsigned short port,
        sfu::RtpFaultConfig fault_config = { },
        std::shared_ptr<ApiAuthenticator> authenticator = nullptr);

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

    // Registers one voice track with the room router. The callback reports
    // pre-negotiated output-slot assignments back to the owning session.
    void register_voice_track(const driscord::PeerId& owner,
        const driscord::RoomId& room_id,
        std::shared_ptr<rtc::Track> track,
        std::function<void(std::string, std::optional<driscord::PeerId>)>
            send_binding);

    // Registers one audio/video track with the room screen router. Output
    // tracks are paired by slot and report the same publisher for both mids.
    void register_screen_track(const driscord::PeerId& owner,
        const driscord::RoomId& room_id,
        std::shared_ptr<rtc::Track> track,
        std::function<void(std::string, std::optional<driscord::PeerId>)>
            send_binding);

    // ICE configuration handed to every per-session PeerConnection.
    const rtc::Configuration& rtc_config() const { return rtc_config_; }

    // Null in anonymous mode: every upgrade and HTTP read is then unguarded,
    // which is why the server binary refuses to start that way by default.
    const std::shared_ptr<ApiAuthenticator>& authenticator() const
    {
        return authenticator_;
    }

    // Total connected sessions across all rooms (for tests).
    size_t active_sessions() const;
    // Sessions in a specific room (for tests).
    size_t active_sessions(const driscord::RoomId& room_id) const;

    void add_streaming_peer(const driscord::PeerId& id,
        const driscord::RoomId& room_id);
    void remove_streaming_peer(const driscord::PeerId& id,
        const driscord::RoomId& room_id);

    // A watcher subscribes to explicit screen publishers. Keeping the target
    // set server-side avoids filling slots/downlink with unselected streams.
    void add_video_watcher(const driscord::PeerId& id,
        const driscord::RoomId& room_id,
        const driscord::PeerId& publisher_id);
    void remove_video_watcher(const driscord::PeerId& id,
        const driscord::RoomId& room_id,
        const driscord::PeerId& publisher_id);

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
        std::unordered_map<driscord::PeerId,
            std::unordered_set<driscord::PeerId>>
            video_watchers;
        std::shared_ptr<VoiceRouter> voice_router;
        std::shared_ptr<ScreenRouter> screen_router;
    };

    mutable std::mutex rooms_mutex_;
    std::unordered_map<driscord::RoomId, Room> rooms_;

    // Built once at construction. The UDP port range ICE binds to is taken
    // from DRISCORD_ICE_PORT_MIN/MAX so deployments behind NAT or in Docker
    // can pin and forward a known range.
    rtc::Configuration rtc_config_;
    sfu::RtpFaultConfig fault_config_;
    std::shared_ptr<ApiAuthenticator> authenticator_;
};

} // namespace driscord
