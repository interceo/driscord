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

    std::optional<std::string> register_and_build_welcome(
        const driscord::PeerId& id,
        const driscord::RoomId& room_id,
        std::shared_ptr<Session> s);

    void unregister_session(const driscord::PeerId& id,
        const driscord::RoomId& room_id);

    void broadcast(const driscord::PeerId& from_id,
        const driscord::RoomId& room_id,
        const std::string& msg);

    void send_to(const driscord::PeerId& target_id,
        const driscord::RoomId& room_id,
        const std::string& msg);

    void register_voice_track(const driscord::PeerId& owner,
        const driscord::RoomId& room_id,
        std::shared_ptr<rtc::Track> track,
        std::function<void(std::string, std::optional<driscord::PeerId>)>
            send_binding);

    void register_screen_track(const driscord::PeerId& owner,
        const driscord::RoomId& room_id,
        std::shared_ptr<rtc::Track> track,
        std::function<void(std::string, std::optional<driscord::PeerId>)>
            send_binding);

    const rtc::Configuration& rtc_config() const { return rtc_config_; }

    const std::shared_ptr<ApiAuthenticator>& authenticator() const
    {
        return authenticator_;
    }

    size_t active_sessions() const;
    size_t active_sessions(const driscord::RoomId& room_id) const;

    void add_streaming_peer(const driscord::PeerId& id,
        const driscord::RoomId& room_id);
    void remove_streaming_peer(const driscord::PeerId& id,
        const driscord::RoomId& room_id);

    void add_video_watcher(const driscord::PeerId& id,
        const driscord::RoomId& room_id,
        const driscord::PeerId& publisher_id);
    void remove_video_watcher(const driscord::PeerId& id,
        const driscord::RoomId& room_id,
        const driscord::PeerId& publisher_id);

    std::string presence_json() const;
    std::string media_stats_json() const;

    void update_fault_config(sfu::RtpFaultConfig fault_config);

private:
    void do_accept();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
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

    rtc::Configuration rtc_config_;
    sfu::RtpFaultConfig fault_config_;
    std::shared_ptr<ApiAuthenticator> authenticator_;
};

}
