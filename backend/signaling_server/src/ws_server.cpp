#include "ws_server.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <iomanip>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "bounded_queue.hpp"
#include "channel_labels.hpp"
#include "log.hpp"
#include "protocol.hpp"
#include "session_fsm_table.hpp"
#include "signaling_protocol.hpp"

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
    // Bridges the state machine's Actions onto this session. Nested so the
    // machine can reach the session's private members directly.
    struct FsmActions : session_fsm::Actions {
        explicit FsmActions(Session& s)
            : self(s)
        {
        }
        void accept_offer(const std::string& sdp) override
        {
            self.build_peer_connection(sdp);
        }
        void apply_candidate(const std::string& candidate,
            const std::string& mid) override
        {
            self.add_remote_candidate(candidate, mid);
        }
        void close_peer_connection() override { self.close_peer_connection(); }
        void log_ignored(const char* what) override
        {
            LOG_WARNING() << "session " << self.id_.value
                          << ": ignoring out-of-phase " << what;
        }
        Session& self;
    };

public:
    Session(tcp::socket&& socket, std::shared_ptr<WebSocketServer> server)
        : ws_(std::move(socket))
        , id_(generate_id())
        , server_(std::move(server))
        , fsm_actions_(*this)
        , fsm_(static_cast<session_fsm::Actions&>(fsm_actions_))
    {
    }

    // Tears the PeerConnection down on whichever thread drops the last
    // reference, in case the session went away through an error path that
    // never reached on_close().
    ~Session() { close_peer_connection(); }

    const driscord::PeerId& id() const { return id_; }
    const driscord::RoomId& room_id() const { return room_id_; }
    const driscord::Username& username() const { return username_; }

    void start()
    {
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

    // Sends an already-framed media packet on this session's DataChannel with
    // the given label. Called from other sessions' DataChannel threads during
    // fan-out; a closed or not-yet-open channel is silently skipped.
    void send_media(channel::MediaChannel label, const std::string& payload)
    {
        std::shared_ptr<rtc::DataChannel> dc;
        {
            std::scoped_lock lk(pc_mutex_);
            auto it = channels_.find(label);
            if (it != channels_.end()) {
                dc = it->second;
            }
        }
        if (!dc || !dc->isOpen()) {
            return;
        }
        try {
            dc->send(reinterpret_cast<const std::byte*>(payload.data()),
                payload.size());
        } catch (const std::exception& e) {
            LOG_ERROR() << "send_media[" << channel::to_label(label) << "]["
                        << id_.value
                        << "]: " << e.what();
        }
    }

    void close_peer_connection()
    {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::unordered_map<channel::MediaChannel,
            std::shared_ptr<rtc::DataChannel>>
            chans;
        {
            std::scoped_lock lk(pc_mutex_);
            pc = std::move(pc_);
            chans = std::move(channels_);
            pc_.reset();
            channels_.clear();
        }
        for (auto& [_, dc] : chans) {
            if (dc) {
                dc->close();
            }
        }
        if (pc) {
            pc->close();
        }
    }

public:
    // Feeds an event to the state machine. Callable from any thread — the
    // event is posted onto this session's strand, which is the only place the
    // machine and the PeerConnection lifecycle are ever touched. Holding a
    // strong reference here is deliberate: it keeps ~Session on an asio thread
    // rather than on an RTC worker, where tearing down the Beast stream would
    // race the io_context.
    void post_fsm(session_fsm::Event ev)
    {
        boost::asio::post(ws_.get_executor(),
            [self = shared_from_this(), ev = std::move(ev)]() mutable {
                std::visit([&self](auto&& e) { self->fsm_.process_event(e); },
                    ev);
            });
    }

private:
    // Builds the PeerConnection for this client and answers its offer. The
    // client is always the offerer and always creates the DataChannels.
    void build_peer_connection(const std::string& sdp)
    {
        std::shared_ptr<rtc::PeerConnection> pc;
        try {
            pc = std::make_shared<rtc::PeerConnection>(server_->rtc_config());
        } catch (const std::exception& e) {
            LOG_ERROR() << "peer connection create failed [" << id_.value
                        << "]: " << e.what();
            return;
        }

        // Weak, deliberately: the session owns the PeerConnection, so a strong
        // capture here would form a cycle. The session would then outlive the
        // room that holds it and be destroyed on an RTC worker thread — tearing
        // down its Beast socket after the io_context is already gone.
        std::weak_ptr<Session> weak = weak_from_this();

        pc->onLocalDescription([weak](rtc::Description desc) {
            auto self = weak.lock();
            if (!self) {
                return;
            }
            const auto type = desc.typeString();
            if (type == "answer") {
                self->send(std::make_shared<std::string>(
                    signaling::dump(signaling::Answer { std::string(desc) })));
            } else if (type == "offer") {
                self->send(std::make_shared<std::string>(
                    signaling::dump(signaling::Offer { std::string(desc) })));
            }
        });

        pc->onLocalCandidate([weak](rtc::Candidate cand) {
            auto self = weak.lock();
            if (!self) {
                return;
            }
            self->send(std::make_shared<std::string>(
                signaling::dump(signaling::Candidate { std::string(cand),
                    cand.mid() })));
        });

        pc->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> dc) {
            if (auto self = weak.lock()) {
                self->setup_channel(std::move(dc));
            }
        });

        pc->onStateChange([weak, id = id_](rtc::PeerConnection::State state) {
            LOG_INFO() << "session " << id.value << " pc state: "
                       << static_cast<int>(state);
            if (state != rtc::PeerConnection::State::Failed) {
                return;
            }
            if (auto self = weak.lock()) {
                self->post_fsm(session_fsm::PcFailed { });
            }
        });

        {
            std::scoped_lock lk(pc_mutex_);
            pc_ = pc;
        }

        try {
            pc->setRemoteDescription(
                rtc::Description(sdp, rtc::Description::Type::Offer));
        } catch (const std::exception& e) {
            LOG_ERROR() << "setRemoteDescription failed [" << id_.value
                        << "]: " << e.what();
        }
    }

    void add_remote_candidate(const std::string& candidate,
        const std::string& mid)
    {
        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::scoped_lock lk(pc_mutex_);
            pc = pc_;
        }
        if (!pc) {
            LOG_WARNING() << "candidate with no peer connection [" << id_.value
                          << "], dropping";
            return;
        }
        try {
            pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
        } catch (const std::exception& e) {
            LOG_ERROR() << "addRemoteCandidate failed [" << id_.value
                        << "]: " << e.what();
        }
    }

    void setup_channel(std::shared_ptr<rtc::DataChannel> dc)
    {
        const auto parsed_label = channel::parse_label(dc->label());
        if (!parsed_label) {
            LOG_WARNING() << "session " << id_.value
                          << " rejected unknown channel '" << dc->label() << "'";
            dc->close();
            return;
        }

        const auto label = *parsed_label;
        const auto label_text = channel::to_label(label);
        LOG_INFO() << "session " << id_.value << " opened channel '"
                   << label_text << "'";
        {
            std::scoped_lock lk(pc_mutex_);
            channels_[label] = dc;
        }

        std::weak_ptr<Session> weak = weak_from_this();
        post_fsm(session_fsm::ChannelOpened { });
        dc->onMessage([weak, label](rtc::message_variant msg) {
            auto* bin = std::get_if<rtc::binary>(&msg);
            if (!bin) {
                return;
            }
            auto self = weak.lock();
            if (!self) {
                return;
            }
            self->server_->route_media(self->id_, self->room_id_, label,
                reinterpret_cast<const uint8_t*>(bin->data()), bin->size());
        });

        dc->onError([label_text, id = id_](std::string error) {
            LOG_ERROR() << "dc '" << label_text << "' error [" << id.value
                        << "]: " << error;
        });
    }

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
                post_fsm(session_fsm::OfferReceived { message.sdp });
            } else if constexpr (std::is_same_v<T, signaling::Candidate>) {
                post_fsm(session_fsm::RemoteCandidate {
                    message.candidate, message.sdp_mid });
            } else if constexpr (std::is_same_v<T, signaling::StreamingStart>) {
                server_->add_streaming_peer(id_, room_id_);
                server_->broadcast(id_, room_id_,
                    signaling::dump(signaling::StreamingStart { id_ }));
            } else if constexpr (std::is_same_v<T, signaling::StreamingStop>) {
                server_->remove_streaming_peer(id_, room_id_);
                server_->broadcast(id_, room_id_,
                    signaling::dump(signaling::StreamingStop { id_ }));
            } else if constexpr (std::is_same_v<T, signaling::WatchStart>) {
                server_->add_video_watcher(id_, room_id_);
                server_->broadcast(id_, room_id_,
                    signaling::dump(signaling::WatchStart { id_ }));
            } else if constexpr (std::is_same_v<T, signaling::WatchStop>) {
                server_->remove_video_watcher(id_, room_id_);
                server_->broadcast(id_, room_id_,
                    signaling::dump(signaling::WatchStop { id_ }));
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
        close_peer_connection();
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

    // Guards pc_ and channels_, both touched from libdatachannel's threads
    // and from other sessions' fan-out.
    std::mutex pc_mutex_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::unordered_map<channel::MediaChannel, std::shared_ptr<rtc::DataChannel>>
        channels_;

    // Only ever touched on this session's strand — see post_fsm().
    FsmActions fsm_actions_;
    boost::sml::sm<session_fsm::Machine> fsm_;
};

// --- WebSocketServer ---------------------------------------------------------

WebSocketServer::WebSocketServer(boost::asio::io_context& io_context,
    unsigned short port)
    : io_context_(io_context)
    , acceptor_(io_context_, tcp::endpoint(tcp::v4(), port))
{
    // No STUN/TURN: the server is the answerer and is expected to be publicly
    // reachable, so its host candidates are enough — clients always dial out.
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
    // Must match the client's ceiling: a whole encoded video frame arrives as
    // one message and is fragmented by SCTP, not by the application.
    rtc_config_.maxMessageSize = 256 * 1024;
    LOG_INFO() << "ICE UDP port range: " << rtc_config_.portRangeBegin << "-"
               << rtc_config_.portRangeEnd;
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
    // dropping the references. route_media() runs on libdatachannel's threads
    // and holds strong references to its targets while it fans out, so simply
    // clearing the map could leave the final reference — and therefore
    // ~Session, which destroys a Beast stream — to be released on an RTC
    // thread, potentially after the io_context is gone. close_peer_connection()
    // joins those callbacks, so the last reference is guaranteed to die here.
    std::vector<std::shared_ptr<Session>> sessions;
    {
        std::scoped_lock lk(rooms_mutex_);
        for (auto& [_, room] : rooms_) {
            for (auto& [__, session] : room.sessions) {
                sessions.push_back(session);
            }
        }
        rooms_.clear();
    }
    for (auto& session : sessions) {
        session->close_peer_connection();
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

        signaling::Welcome welcome;
        welcome.id = id;
        existing.reserve(room.sessions.size());
        for (auto& [pid, session] : room.sessions) {
            welcome.peers.push_back({
                pid,
                session ? session->username() : driscord::Username {},
            });
            existing.push_back(session);
        }

        for (auto& sid : room.streaming_peers) {
            welcome.streaming_peers.push_back(sid);
        }

        welcome_payload = signaling::dump(welcome);
        new_username = s ? s->username() : driscord::Username {};

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
            { "packetsIn", room.media_packets_in },
            { "packetsOut", room.media_packets_out },
            { "bytesIn", room.media_bytes_in },
            { "bytesOut", room.media_bytes_out },
            { "packetsDropped", room.media_packets_dropped },
        };
    }
    return out.dump();
}

void WebSocketServer::unregister_session(const driscord::PeerId& id,
    const driscord::RoomId& room_id)
{
    std::vector<std::shared_ptr<Session>> remaining;
    {
        std::scoped_lock lk(rooms_mutex_);
        auto rit = rooms_.find(room_id);
        if (rit == rooms_.end()) {
            return;
        }
        auto& room = rit->second;
        room.sessions.erase(id);
        room.streaming_peers.erase(id);
        room.video_watchers.erase(id);
        remaining.reserve(room.sessions.size());
        for (auto& [_, session] : room.sessions) {
            remaining.push_back(session);
        }
        // Remove empty rooms to avoid unbounded map growth.
        if (room.sessions.empty()) {
            rooms_.erase(rit);
        }
    }

    auto msg = std::make_shared<std::string>(
        signaling::dump(signaling::PeerLeft { id }));
    for (auto& session : remaining) {
        session->send(msg);
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

void WebSocketServer::route_media(const driscord::PeerId& from_id,
    const driscord::RoomId& room_id,
    channel::MediaChannel label,
    const uint8_t* data,
    size_t len)
{
    if (!data || len == 0) {
        return;
    }

    auto encoded = protocol::encode_relayed_media(from_id, data, len);
    if (!encoded) {
        LOG_WARNING() << "failed to frame media packet from " << from_id.value;
        std::scoped_lock lk(rooms_mutex_);
        auto rit = rooms_.find(room_id);
        if (rit != rooms_.end()) {
            rit->second.media_packets_dropped++;
        }
        return;
    }
    const std::string& packet = encoded.value();

    std::vector<std::shared_ptr<Session>> targets;
    {
        std::scoped_lock lk(rooms_mutex_);
        auto rit = rooms_.find(room_id);
        if (rit == rooms_.end()) {
            return;
        }
        auto& room = rit->second;
        room.media_packets_in++;
        room.media_bytes_in += len;

        if (channel::is_watcher_gated(label)) {
            targets.reserve(room.video_watchers.size());
            for (auto& pid : room.video_watchers) {
                if (pid == from_id) {
                    continue;
                }
                auto sit = room.sessions.find(pid);
                if (sit != room.sessions.end()) {
                    targets.push_back(sit->second);
                }
            }
        } else {
            targets.reserve(room.sessions.size());
            for (auto& [pid, session] : room.sessions) {
                if (pid != from_id) {
                    targets.push_back(session);
                }
            }
        }
        room.media_packets_out += targets.size();
        room.media_bytes_out += packet.size() * targets.size();
    }

    for (auto& session : targets) {
        session->send_media(label, packet);
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
    std::scoped_lock lk(rooms_mutex_);
    // find, not operator[]: a peer only streams in a room it already joined, so
    // the room exists. Using operator[] here would let a stray call resurrect an
    // empty Room that unregister_session (keyed on live sessions) never reaps.
    auto rit = rooms_.find(room_id);
    if (rit != rooms_.end()) {
        rit->second.streaming_peers.insert(id);
    }
}

void WebSocketServer::remove_streaming_peer(const driscord::PeerId& id,
    const driscord::RoomId& room_id)
{
    std::scoped_lock lk(rooms_mutex_);
    auto rit = rooms_.find(room_id);
    if (rit != rooms_.end()) {
        rit->second.streaming_peers.erase(id);
    }
}

void WebSocketServer::add_video_watcher(const driscord::PeerId& id,
    const driscord::RoomId& room_id)
{
    std::scoped_lock lk(rooms_mutex_);
    // See add_streaming_peer: find, not operator[], so a watch_start can't
    // conjure an empty room that never gets cleaned up.
    auto rit = rooms_.find(room_id);
    if (rit != rooms_.end()) {
        rit->second.video_watchers.insert(id);
    }
}

void WebSocketServer::remove_video_watcher(const driscord::PeerId& id,
    const driscord::RoomId& room_id)
{
    std::scoped_lock lk(rooms_mutex_);
    auto rit = rooms_.find(room_id);
    if (rit != rooms_.end()) {
        rit->second.video_watchers.erase(id);
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
