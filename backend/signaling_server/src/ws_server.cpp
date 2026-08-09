#include "ws_server.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "bounded_queue.hpp"
#include "config.hpp"
#include "log.hpp"
#include "media_connections.hpp"
#include "screen_router.hpp"
#include "signaling_protocol.hpp"
#include "voice_router.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;

using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

namespace {

driscord::PeerId generate_id()
{
    static std::mt19937 rng { std::random_device { }() };
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << dist(rng);
    return driscord::PeerId { ss.str() };
}

// Extracts the room identifier from a WebSocket upgrade request target.
// "/channels/42"      → "42"
// "/channels/42?x=1"  → "42"
// anything else       → "default"
driscord::RoomId parse_room_id(std::string_view target)
{
    constexpr std::string_view kPrefix = "/channels/";
    if (target.size() > kPrefix.size()
        && target.substr(0, kPrefix.size()) == kPrefix) {
        auto rest = target.substr(kPrefix.size());
        auto q = rest.find('?');
        if (q != std::string_view::npos) {
            rest = rest.substr(0, q);
        }
        while (!rest.empty() && rest.back() == '/') {
            rest.remove_suffix(1);
        }
        if (!rest.empty()) {
            return driscord::RoomId { std::string(rest) };
        }
    }
    return driscord::RoomId { "default" };
}

// Decode percent-escapes and '+' in a URL query value.
std::string url_decode(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < s.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9')
                    return h - '0';
                if (h >= 'a' && h <= 'f')
                    return h - 'a' + 10;
                if (h >= 'A' && h <= 'F')
                    return h - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]);
            int lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Extract the value of a query parameter from a request target.
// parse_query_param("/channels/42?u=alice&x=1", "u") → "alice"
std::string parse_query_param(std::string_view target, std::string_view key)
{
    auto q = target.find('?');
    if (q == std::string_view::npos)
        return { };
    auto qs = target.substr(q + 1);
    while (!qs.empty()) {
        auto amp = qs.find('&');
        auto pair = (amp == std::string_view::npos) ? qs : qs.substr(0, amp);
        auto eq = pair.find('=');
        if (eq != std::string_view::npos && pair.substr(0, eq) == key) {
            return url_decode(pair.substr(eq + 1));
        }
        if (amp == std::string_view::npos)
            break;
        qs = qs.substr(amp + 1);
    }
    return { };
}

constexpr size_t kMaxMessageSize = 256 * 1024;
constexpr size_t kMaxWriteQueueSize = 128;

} // namespace

namespace driscord {

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket&& socket, std::shared_ptr<WebSocketServer> server)
        : ws_(std::move(socket))
        , id_(generate_id())
        , server_(std::move(server))
    {
    }

    // Tears the PeerConnection down on whichever thread drops the last
    // reference, in case the session went away through an error path that
    // never reached on_close().
    ~Session() { close_media_connections(); }

    const driscord::PeerId& id() const { return id_; }
    const driscord::RoomId& room_id() const { return room_id_; }
    const driscord::Username& username() const { return username_; }

    void start()
    {
        const std::weak_ptr<Session> weak = weak_from_this();
        media_connections_ = std::make_shared<MediaConnections>(
            server_->rtc_config(), id_, [weak](std::string message) {
                if (auto self = weak.lock()) {
                    self->send(
                        std::make_shared<std::string>(std::move(message)));
                } }, [weak](signaling::ConnectionId connection, std::shared_ptr<rtc::Track> track) {
                if (auto self = weak.lock()) {
                    const std::weak_ptr<Session> binding_owner = self;
                    auto send_binding = [binding_owner, connection](
                                            std::string mid,
                                            std::optional<driscord::PeerId>
                                                publisher) {
                        if (auto session = binding_owner.lock()) {
                            session->send(std::make_shared<std::string>(
                                signaling::dump(signaling::TrackBinding {
                                    std::move(mid), std::move(publisher),
                                    connection })));
                        }
                    };
                    if (connection == signaling::ConnectionId::Voice) {
                        self->server_->register_voice_track(self->id_,
                            self->room_id_, std::move(track),
                            std::move(send_binding));
                    } else if (connection
                        == signaling::ConnectionId::Screen) {
                        self->server_->register_screen_track(self->id_,
                            self->room_id_, std::move(track),
                            std::move(send_binding));
                    }
                } });
        ws_.set_option(
            websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.set_option(
            websocket::stream_base::decorator([](websocket::response_type& res) {
                res.set(http::field::server, "driscord/ws");
            }));
        ws_.read_message_max(kMaxMessageSize);

        // Read the HTTP upgrade request first so we can extract the URL path
        // (which carries the channel/room ID) before accepting the WebSocket.
        auto req = std::make_shared<http::request<http::string_body>>();
        http::async_read(ws_.next_layer(), buffer_, *req,
            beast::bind_front_handler(&Session::on_http_read, shared_from_this(),
                req));
    }

    // send() may be called from any thread. Protected by write_mutex_ and
    // posts the actual write onto this session's strand.
    void send(std::shared_ptr<std::string> msg)
    {
        enqueue({ std::move(msg), true });
    }

    void close_media_connections()
    {
        std::shared_ptr<MediaConnections> media_connections;
        {
            std::scoped_lock lock(media_mutex_);
            media_connections = std::move(media_connections_);
        }
        if (media_connections) {
            media_connections->close();
        }
    }

private:
    struct OutboundMessage {
        std::shared_ptr<std::string> payload;
        bool text = true;
    };

    void enqueue(OutboundMessage msg)
    {
        bool start_write = false;
        {
            std::scoped_lock lk(write_mutex_);
            const auto pushed = write_queue_.push_back(std::move(msg));
            if (pushed == utils::QueuePushResult::DroppedFull) {
                LOG_WARNING() << "write queue overflow for " << id_.value
                              << ", dropping message";
                return;
            }
            if (pushed == utils::QueuePushResult::Closed) {
                return;
            }
            start_write = (write_queue_.size() == 1);
        }
        if (start_write) {
            boost::asio::post(ws_.get_executor(),
                [self = shared_from_this()]() { self->do_write(); });
        }
    }
    // Called after we've read the HTTP upgrade request. Either responds to a
    // plain HTTP GET (currently only /presence) or extracts the room_id from
    // the request target and completes the WebSocket handshake.
    void on_http_read(std::shared_ptr<http::request<http::string_body>> req,
        beast::error_code ec,
        std::size_t)
    {
        if (ec) {
            LOG_ERROR() << "http read [" << id_.value << "]: " << ec.message();
            return;
        }

        // Plain HTTP request (no Upgrade: websocket header) → presence endpoint.
        if (!websocket::is_upgrade(*req)) {
            handle_http_request(std::move(req));
            return;
        }

        room_id_ = parse_room_id(req->target());
        username_ = driscord::Username { parse_query_param(req->target(), "u") };
        LOG_INFO() << "session " << id_.value << " joining room "
                   << room_id_.value << " as '" << username_.value << "'";

        ws_.async_accept(*req,
            beast::bind_front_handler(&Session::on_accept, shared_from_this()));
    }

    void handle_http_request(
        std::shared_ptr<http::request<http::string_body>> req)
    {
        auto res = std::make_shared<http::response<http::string_body>>();
        res->version(req->version());
        res->keep_alive(false);
        res->set(http::field::server, "driscord/ws");
        res->set("Access-Control-Allow-Origin", "*");

        std::string_view target = req->target();
        auto path_end = target.find('?');
        std::string_view path = (path_end == std::string_view::npos)
            ? target
            : target.substr(0, path_end);

        if (req->method() == http::verb::get && path == "/presence") {
            res->result(http::status::ok);
            res->set(http::field::content_type, "application/json");
            res->body() = server_->presence_json();
        } else if (req->method() == http::verb::get && path == "/media_stats") {
            res->result(http::status::ok);
            res->set(http::field::content_type, "application/json");
            res->body() = server_->media_stats_json();
        } else {
            res->result(http::status::not_found);
            res->set(http::field::content_type, "text/plain");
            res->body() = "not found";
        }
        res->prepare_payload();

        http::async_write(ws_.next_layer(), *res,
            [self = shared_from_this(), res](beast::error_code wec, std::size_t) {
                if (wec) {
                    LOG_ERROR() << "http write [" << self->id_.value
                                << "]: " << wec.message();
                }
                beast::error_code shut;
                self->ws_.next_layer().socket().shutdown(tcp::socket::shutdown_send, shut);
            });
    }

    // Runs on the session's strand.
    void do_write()
    {
        OutboundMessage msg;
        {
            std::scoped_lock lk(write_mutex_);
            if (write_queue_.empty()) {
                return;
            }
            msg = write_queue_.front();
        }
        ws_.text(msg.text);
        ws_.async_write(
            boost::asio::buffer(*msg.payload),
            beast::bind_front_handler(&Session::on_write, shared_from_this()));
    }

    // Runs on the session's strand (async_write completion handler).
    void on_write(beast::error_code ec, std::size_t)
    {
        if (ec) {
            LOG_ERROR() << "write error [" << id_.value << "]: "
                        << ec.message();
            std::scoped_lock lk(write_mutex_);
            write_queue_.close();
            return;
        }
        bool have_more = false;
        {
            std::scoped_lock lk(write_mutex_);
            if (!write_queue_.empty()) {
                write_queue_.pop_front();
            }
            have_more = !write_queue_.empty();
        }
        if (have_more) {
            do_write();
        }
    }

    void on_accept(beast::error_code ec)
    {
        if (ec) {
            LOG_ERROR() << "accept [" << id_.value << "]: " << ec.message();
            return;
        }

        auto welcome = server_->register_and_build_welcome(id_, room_id_,
            shared_from_this());
        send(std::make_shared<std::string>(std::move(welcome)));

        LOG_INFO() << "session " << id_.value << " connected (room="
                   << room_id_.value << ")";
        do_read();
    }

    void do_read()
    {
        ws_.async_read(buffer_,
            beast::bind_front_handler(&Session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t)
    {
        if (ec == websocket::error::closed) {
            on_close();
            return;
        }
        if (ec) {
            LOG_ERROR() << "read [" << id_.value << "]: " << ec.message();
            on_close();
            return;
        }

        const auto* raw_data = static_cast<const uint8_t*>(buffer_.data().data());
        const size_t raw_len = buffer_.data().size();
        std::string_view raw { reinterpret_cast<const char*>(raw_data), raw_len };

        auto parsed = signaling::parse(raw);
        if (!parsed) {
            LOG_ERROR() << "signaling parse error [" << id_.value << "]: "
                        << signaling::to_string(parsed.error());
            buffer_.consume(buffer_.size());
            do_read();
            return;
        }

        std::visit([this](auto&& message) {
            using T = std::decay_t<decltype(message)>;

            // SDP/ICE are addressed to the server itself — it terminates the
            // PeerConnection, so these are consumed here and never forwarded
            // to other clients.
            if constexpr (std::is_same_v<T, signaling::Offer>) {
                std::shared_ptr<MediaConnections> connections;
                {
                    std::scoped_lock lock(media_mutex_);
                    connections = media_connections_;
                }
                if (connections) {
                    connections->accept_offer(message.connection, message.sdp);
                }
            } else if constexpr (std::is_same_v<T, signaling::Candidate>) {
                std::shared_ptr<MediaConnections> connections;
                {
                    std::scoped_lock lock(media_mutex_);
                    connections = media_connections_;
                }
                if (connections) {
                    connections->add_remote_candidate(message.connection,
                        message.candidate, message.sdp_mid);
                }
            } else if constexpr (std::is_same_v<T, signaling::StreamingStart>) {
                server_->add_streaming_peer(id_, room_id_);
                server_->broadcast(id_, room_id_,
                    signaling::dump(signaling::StreamingStart { id_ }));
            } else if constexpr (std::is_same_v<T, signaling::StreamingStop>) {
                server_->remove_streaming_peer(id_, room_id_);
                server_->broadcast(id_, room_id_,
                    signaling::dump(signaling::StreamingStop { id_ }));
            } else if constexpr (std::is_same_v<T, signaling::WatchStart>) {
                server_->add_video_watcher(id_, room_id_, message.peer_id);
            } else if constexpr (std::is_same_v<T, signaling::WatchStop>) {
                server_->remove_video_watcher(id_, room_id_, message.peer_id);
            } else {
                LOG_WARNING() << "unexpected client signaling message ["
                              << id_.value << "]";
            }
        },
            parsed.value());

        buffer_.consume(buffer_.size());
        do_read();
    }

    void on_close()
    {
        close_media_connections();
        {
            std::scoped_lock lk(write_mutex_);
            write_queue_.close();
        }
        server_->unregister_session(id_, room_id_);
        LOG_INFO() << "session " << id_.value << " disconnected (room="
                   << room_id_.value << ")";
    }

    beast::flat_buffer buffer_;
    websocket::stream<beast::tcp_stream> ws_;
    driscord::PeerId id_;
    driscord::RoomId room_id_;
    driscord::Username username_;
    std::shared_ptr<WebSocketServer> server_;
    std::mutex write_mutex_;
    utils::BoundedQueue<OutboundMessage> write_queue_ { kMaxWriteQueueSize };

    // Guards only the ownership hand-off during shutdown. MediaConnections
    // protects the independent voice/screen PeerConnections internally.
    std::mutex media_mutex_;
    std::shared_ptr<MediaConnections> media_connections_;
};

// --- WebSocketServer ---------------------------------------------------------

WebSocketServer::WebSocketServer(boost::asio::io_context& io_context,
    unsigned short port,
    sfu::RtpFaultConfig fault_config)
    : io_context_(io_context)
    , acceptor_(io_context_, tcp::endpoint(tcp::v4(), port))
    , fault_config_(fault_config)
{
    // Pinning the UDP range keeps firewall/Docker port-forwarding tractable.
    auto env_port = [](const char* name, uint16_t fallback) -> uint16_t {
        if (const char* v = std::getenv(name)) {
            try {
                return static_cast<uint16_t>(std::stoi(v));
            } catch (const std::exception&) {
                LOG_WARNING() << name << " is not a valid port, using "
                              << fallback;
            }
        }
        return fallback;
    };
    rtc_config_.portRangeBegin = env_port("DRISCORD_ICE_PORT_MIN", 49160);
    rtc_config_.portRangeEnd = env_port("DRISCORD_ICE_PORT_MAX", 49200);
    LOG_INFO() << "ICE UDP port range: " << rtc_config_.portRangeBegin << "-"
               << rtc_config_.portRangeEnd;

    // Host candidates carry whatever address the SFU's own interfaces have.
    // Under Docker bridge networking that is the container address and behind
    // a home router it is a LAN address, neither of which a remote client can
    // reach, so host candidates alone only work when a public address sits
    // directly on the interface. STUN is what lets the SFU learn its
    // server-reflexive candidate; TURN URLs are accepted here as well.
    //
    // Set DRISCORD_ICE_STUN_URLS to a private STUN/TURN deployment to avoid
    // the public default, or to "none" to advertise host candidates only.
    rtc_config_.iceServers = [] {
        std::vector<rtc::IceServer> servers;
        const char* raw = std::getenv("DRISCORD_ICE_STUN_URLS");
        const std::string value(
            raw != nullptr ? raw : "stun:stun.l.google.com:19302");
        if (value == "none") {
            return servers;
        }
        std::istringstream stream(value);
        std::string url;
        while (std::getline(stream, url, ',')) {
            const auto begin = url.find_first_not_of(" \t");
            if (begin == std::string::npos) {
                continue;
            }
            url = url.substr(begin, url.find_last_not_of(" \t") - begin + 1);
            try {
                servers.emplace_back(url);
            } catch (const std::exception& error) {
                LOG_WARNING() << "Ignoring ICE server '" << url
                              << "': " << error.what();
            }
        }
        return servers;
    }();

    if (rtc_config_.iceServers.empty()) {
        LOG_WARNING() << "No ICE servers configured; clients can only reach "
                         "the SFU if a publicly routable address sits on its "
                         "own interface";
    } else {
        for (const auto& server : rtc_config_.iceServers) {
            LOG_INFO() << "ICE server: " << server.hostname << ":"
                       << server.port;
        }
    }
}

void WebSocketServer::run()
{
    do_accept();
}

void WebSocketServer::stop()
{
    // Close the acceptor on its own executor. do_accept() re-arms it from the
    // io_context thread, and basic_socket_acceptor is not thread-safe, so
    // closing it straight from a caller on another thread races that re-arm
    // and can fault inside the reactor.
    stopping_ = true;
    boost::asio::post(acceptor_.get_executor(), [this] {
        boost::system::error_code ec;
        acceptor_.close(ec);
    });

    // Take the sessions out first, then tear their PeerConnections down before
    // dropping the references, so Beast objects cannot be destroyed later on
    // a libdatachannel callback thread after the io_context is gone.
    std::vector<std::shared_ptr<Session>> sessions;
    std::vector<std::shared_ptr<VoiceRouter>> voice_routers;
    std::vector<std::shared_ptr<ScreenRouter>> screen_routers;
    {
        std::scoped_lock lk(rooms_mutex_);
        for (auto& [_, room] : rooms_) {
            if (room.voice_router) {
                voice_routers.push_back(room.voice_router);
            }
            if (room.screen_router) {
                screen_routers.push_back(room.screen_router);
            }
            for (auto& [__, session] : room.sessions) {
                sessions.push_back(session);
            }
        }
        rooms_.clear();
    }
    for (auto& router : voice_routers) {
        router->close();
    }
    for (auto& router : screen_routers) {
        router->close();
    }
    for (auto& session : sessions) {
        session->close_media_connections();
    }
}

unsigned short WebSocketServer::bound_port() const
{
    boost::system::error_code ec;
    auto endpoint = acceptor_.local_endpoint(ec);
    if (ec) {
        return 0;
    }
    return endpoint.port();
}

std::string WebSocketServer::register_and_build_welcome(
    const driscord::PeerId& id,
    const driscord::RoomId& room_id,
    std::shared_ptr<Session> s)
{
    std::vector<std::shared_ptr<Session>> existing;
    std::string welcome_payload;
    driscord::Username new_username;
    {
        std::scoped_lock lk(rooms_mutex_);

        auto& room = rooms_[room_id];
        if (!room.voice_router) {
            room.voice_router
                = std::make_shared<VoiceRouter>(fault_config_);
        }
        if (!room.screen_router) {
            room.screen_router
                = std::make_shared<ScreenRouter>(fault_config_);
        }

        signaling::Welcome welcome;
        welcome.id = id;
        existing.reserve(room.sessions.size());
        for (auto& [pid, session] : room.sessions) {
            welcome.peers.push_back({
                pid,
                session ? session->username() : driscord::Username { },
            });
            existing.push_back(session);
        }

        for (auto& sid : room.streaming_peers) {
            welcome.streaming_peers.push_back(sid);
        }

        welcome_payload = signaling::dump(welcome);
        new_username = s ? s->username() : driscord::Username { };

        room.sessions.emplace(id, std::move(s));
    }

    auto joined_msg = std::make_shared<std::string>(
        signaling::dump(signaling::PeerJoined { id, new_username }));
    for (auto& session : existing) {
        session->send(joined_msg);
    }

    return welcome_payload;
}

std::string WebSocketServer::presence_json() const
{
    json out = json::object();
    std::scoped_lock lk(rooms_mutex_);
    for (const auto& [room_id, room] : rooms_) {
        json arr = json::array();
        for (const auto& [pid, session] : room.sessions) {
            arr.push_back({
                { "id", pid.value },
                { "username", session ? session->username().value : "" },
            });
        }
        out[room_id.value] = std::move(arr);
    }
    return out.dump();
}

std::string WebSocketServer::media_stats_json() const
{
    json out = json::object();
    std::scoped_lock lk(rooms_mutex_);
    for (const auto& [room_id, room] : rooms_) {
        out[room_id.value] = {
            { "sessions", room.sessions.size() },
            { "streamingPeers", room.streaming_peers.size() },
        };
    }
    return out.dump();
}

void WebSocketServer::unregister_session(const driscord::PeerId& id,
    const driscord::RoomId& room_id)
{
    std::vector<std::shared_ptr<Session>> remaining;
    std::shared_ptr<VoiceRouter> voice_router;
    std::shared_ptr<ScreenRouter> screen_router;
    bool was_streaming = false;
    {
        std::scoped_lock lk(rooms_mutex_);
        auto rit = rooms_.find(room_id);
        if (rit == rooms_.end()) {
            return;
        }
        auto& room = rit->second;
        voice_router = room.voice_router;
        screen_router = room.screen_router;
        room.sessions.erase(id);
        was_streaming = room.streaming_peers.erase(id) > 0;
        room.video_watchers.erase(id);
        for (auto watcher = room.video_watchers.begin();
            watcher != room.video_watchers.end();) {
            watcher->second.erase(id);
            if (watcher->second.empty()) {
                watcher = room.video_watchers.erase(watcher);
            } else {
                ++watcher;
            }
        }
        remaining.reserve(room.sessions.size());
        for (auto& [_, session] : room.sessions) {
            remaining.push_back(session);
        }
        // Remove empty rooms to avoid unbounded map growth.
        if (room.sessions.empty()) {
            rooms_.erase(rit);
        }
    }

    if (voice_router) {
        voice_router->remove_peer(id);
    }
    if (screen_router) {
        screen_router->remove_peer(id);
    }

    std::shared_ptr<std::string> stopped;
    if (was_streaming) {
        stopped = std::make_shared<std::string>(
            signaling::dump(signaling::StreamingStop { id }));
    }
    auto msg = std::make_shared<std::string>(
        signaling::dump(signaling::PeerLeft { id }));
    for (auto& session : remaining) {
        if (stopped) {
            session->send(stopped);
        }
        session->send(msg);
    }
}

void WebSocketServer::register_voice_track(const driscord::PeerId& owner,
    const driscord::RoomId& room_id,
    std::shared_ptr<rtc::Track> track,
    std::function<void(std::string, std::optional<driscord::PeerId>)>
        send_binding)
{
    std::shared_ptr<VoiceRouter> router;
    {
        std::scoped_lock lock(rooms_mutex_);
        const auto room = rooms_.find(room_id);
        if (room != rooms_.end()) {
            router = room->second.voice_router;
        }
    }
    if (router) {
        router->register_track(
            owner, std::move(track), std::move(send_binding));
    }
}

void WebSocketServer::register_screen_track(const driscord::PeerId& owner,
    const driscord::RoomId& room_id,
    std::shared_ptr<rtc::Track> track,
    std::function<void(std::string, std::optional<driscord::PeerId>)>
        send_binding)
{
    std::shared_ptr<ScreenRouter> router;
    {
        std::scoped_lock lock(rooms_mutex_);
        const auto room = rooms_.find(room_id);
        if (room != rooms_.end()) {
            router = room->second.screen_router;
        }
    }
    if (router) {
        router->register_track(
            owner, std::move(track), std::move(send_binding));
    }
}

void WebSocketServer::broadcast(const driscord::PeerId& from_id,
    const driscord::RoomId& room_id,
    const std::string& msg)
{
    std::vector<std::shared_ptr<Session>> targets;
    {
        std::scoped_lock lk(rooms_mutex_);
        auto rit = rooms_.find(room_id);
        if (rit == rooms_.end()) {
            return;
        }
        auto& room = rit->second;
        targets.reserve(room.sessions.size());
        for (auto& [pid, session] : room.sessions) {
            if (pid != from_id) {
                targets.push_back(session);
            }
        }
    }
    auto shared_msg = std::make_shared<std::string>(msg);
    for (auto& session : targets) {
        session->send(shared_msg);
    }
}

void WebSocketServer::send_to(const driscord::PeerId& target_id,
    const driscord::RoomId& room_id,
    const std::string& msg)
{
    std::shared_ptr<Session> target;
    {
        std::scoped_lock lk(rooms_mutex_);
        auto rit = rooms_.find(room_id);
        if (rit == rooms_.end()) {
            return;
        }
        auto it = rit->second.sessions.find(target_id);
        if (it != rit->second.sessions.end()) {
            target = it->second;
        }
    }
    if (target) {
        target->send(std::make_shared<std::string>(msg));
    }
}

size_t WebSocketServer::active_sessions() const
{
    std::scoped_lock lk(rooms_mutex_);
    size_t total = 0;
    for (auto& [_, room] : rooms_) {
        total += room.sessions.size();
    }
    return total;
}

size_t WebSocketServer::active_sessions(const driscord::RoomId& room_id) const
{
    std::scoped_lock lk(rooms_mutex_);
    auto it = rooms_.find(room_id);
    return it != rooms_.end() ? it->second.sessions.size() : 0;
}

void WebSocketServer::add_streaming_peer(const driscord::PeerId& id,
    const driscord::RoomId& room_id)
{
    std::shared_ptr<ScreenRouter> router;
    {
        std::scoped_lock lk(rooms_mutex_);
        // find, not operator[]: a peer only streams in a room it already joined, so
        // the room exists. Using operator[] here would let a stray call resurrect an
        // empty Room that unregister_session (keyed on live sessions) never reaps.
        auto rit = rooms_.find(room_id);
        if (rit != rooms_.end()) {
            rit->second.streaming_peers.insert(id);
            router = rit->second.screen_router;
        }
    }
    if (router) {
        router->set_streaming(id, true);
    }
}

void WebSocketServer::remove_streaming_peer(const driscord::PeerId& id,
    const driscord::RoomId& room_id)
{
    std::shared_ptr<ScreenRouter> router;
    {
        std::scoped_lock lk(rooms_mutex_);
        auto rit = rooms_.find(room_id);
        if (rit != rooms_.end()) {
            rit->second.streaming_peers.erase(id);
            router = rit->second.screen_router;
        }
    }
    if (router) {
        router->set_streaming(id, false);
    }
}

void WebSocketServer::add_video_watcher(const driscord::PeerId& id,
    const driscord::RoomId& room_id,
    const driscord::PeerId& publisher_id)
{
    std::shared_ptr<ScreenRouter> router;
    std::shared_ptr<Session> watcher_session;
    std::optional<signaling::WatchRejectReason> rejection;
    {
        std::scoped_lock lk(rooms_mutex_);
        // See add_streaming_peer: find, not operator[], so a watch_start can't
        // conjure an empty room that never gets cleaned up.
        auto rit = rooms_.find(room_id);
        if (rit != rooms_.end()) {
            const auto watcher = rit->second.sessions.find(id);
            if (watcher == rit->second.sessions.end()) {
                return;
            }
            watcher_session = watcher->second;
            auto& room = rit->second;
            if (!room.sessions.contains(publisher_id) || id == publisher_id) {
                rejection = signaling::WatchRejectReason::UnknownPeer;
            } else if (!room.streaming_peers.contains(publisher_id)) {
                rejection = signaling::WatchRejectReason::NotStreaming;
            } else {
                auto& watched = room.video_watchers[id];
                if (watched.contains(publisher_id)) {
                    return;
                }
                if (watched.size()
                    >= stream_defaults::kScreenReceiveSlots) {
                    rejection = signaling::WatchRejectReason::Capacity;
                } else {
                    watched.insert(publisher_id);
                    router = room.screen_router;
                }
            }
        }
    }
    if (rejection && watcher_session) {
        watcher_session->send(std::make_shared<std::string>(signaling::dump(
            signaling::WatchRejected { publisher_id, *rejection })));
        return;
    }
    if (router) {
        router->set_watching(id, publisher_id, true);
    }
}

void WebSocketServer::remove_video_watcher(const driscord::PeerId& id,
    const driscord::RoomId& room_id,
    const driscord::PeerId& publisher_id)
{
    std::shared_ptr<ScreenRouter> router;
    {
        std::scoped_lock lk(rooms_mutex_);
        auto rit = rooms_.find(room_id);
        if (rit != rooms_.end()) {
            const auto watcher = rit->second.video_watchers.find(id);
            if (watcher != rit->second.video_watchers.end()) {
                watcher->second.erase(publisher_id);
                if (watcher->second.empty()) {
                    rit->second.video_watchers.erase(watcher);
                }
            }
            router = rit->second.screen_router;
        }
    }
    if (router) {
        router->set_watching(id, publisher_id, false);
    }
}

void WebSocketServer::do_accept()
{
    acceptor_.async_accept(
        boost::asio::make_strand(io_context_),
        [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<Session>(std::move(socket), self)->start();
            }
            if (!self->stopping_ && self->acceptor_.is_open()) {
                self->do_accept();
            }
        });
}

} // namespace driscord
