#include "ws_server.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <deque>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "log.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;

using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

namespace {

std::string generate_id()
{
    static std::mt19937 rng { std::random_device { }() };
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << dist(rng);
    return ss.str();
}

// Extracts the room identifier from a WebSocket upgrade request target.
// "/channels/42"      → "42"
// "/channels/42?x=1"  → "42"
// anything else       → "default"
std::string parse_room_id(std::string_view target)
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
            return std::string(rest);
        }
    }
    return "default";
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

constexpr uint8_t kMediaClientMagic0 = 'D';
constexpr uint8_t kMediaClientMagic1 = 'M';
constexpr uint8_t kMediaRelayMagic0 = 'D';
constexpr uint8_t kMediaRelayMagic1 = 'R';
constexpr uint8_t kMediaVersion = 1;
constexpr size_t kMediaClientHeaderSize = 4;
constexpr size_t kMediaRelayHeaderSize = 6;

struct MediaClientHeader {
    uint8_t kind = 0;
    uint8_t flags = 0;
};

bool parse_media_client_header(const uint8_t* data,
    size_t len,
    MediaClientHeader& out)
{
    if (len < kMediaClientHeaderSize) {
        return false;
    }
    if (data[0] != kMediaClientMagic0 || data[1] != kMediaClientMagic1
        || data[2] != kMediaVersion) {
        return false;
    }
    out.kind = data[3];
    out.flags = 0;
    return out.kind != 0;
}

std::string build_media_relay_packet(const std::string& from_id,
    uint8_t kind,
    const uint8_t* payload,
    size_t payload_len)
{
    const auto from_len = static_cast<uint16_t>(
        std::min<size_t>(from_id.size(), UINT16_MAX));
    std::string out;
    out.resize(kMediaRelayHeaderSize + from_len + payload_len);
    auto* dst = reinterpret_cast<uint8_t*>(out.data());
    dst[0] = kMediaRelayMagic0;
    dst[1] = kMediaRelayMagic1;
    dst[2] = kMediaVersion;
    dst[3] = kind;
    dst[4] = static_cast<uint8_t>(from_len & 0xffu);
    dst[5] = static_cast<uint8_t>((from_len >> 8) & 0xffu);
    std::memcpy(dst + kMediaRelayHeaderSize, from_id.data(), from_len);
    std::memcpy(dst + kMediaRelayHeaderSize + from_len, payload, payload_len);
    return out;
}

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

    const std::string& id() const { return id_; }
    const std::string& room_id() const { return room_id_; }
    const std::string& username() const { return username_; }

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

    void send_binary(std::shared_ptr<std::string> msg)
    {
        enqueue({ std::move(msg), false });
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
            if (write_queue_.size() >= kMaxWriteQueueSize) {
                LOG_WARNING() << "write queue overflow for " << id_
                              << ", dropping message";
                return;
            }
            write_queue_.push_back(std::move(msg));
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
            LOG_ERROR() << "http read [" << id_ << "]: " << ec.message();
            return;
        }

        // Plain HTTP request (no Upgrade: websocket header) → presence endpoint.
        if (!websocket::is_upgrade(*req)) {
            handle_http_request(std::move(req));
            return;
        }

        room_id_ = parse_room_id(req->target());
        username_ = parse_query_param(req->target(), "u");
        LOG_INFO() << "session " << id_ << " joining room " << room_id_
                   << " as '" << username_ << "'";

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
                    LOG_ERROR() << "http write [" << self->id_ << "]: " << wec.message();
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
            LOG_ERROR() << "write error [" << id_ << "]: " << ec.message();
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
            LOG_ERROR() << "accept [" << id_ << "]: " << ec.message();
            return;
        }

        auto welcome = server_->register_and_build_welcome(id_, room_id_,
            shared_from_this());
        send(std::make_shared<std::string>(std::move(welcome)));

        LOG_INFO() << "session " << id_ << " connected (room=" << room_id_ << ")";
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
            LOG_ERROR() << "read [" << id_ << "]: " << ec.message();
            on_close();
            return;
        }

        const auto* raw_data = static_cast<const uint8_t*>(buffer_.data().data());
        const size_t raw_len = buffer_.data().size();

        if (!ws_.got_text()) {
            server_->relay_binary_media(id_, room_id_, raw_data, raw_len);
            buffer_.consume(buffer_.size());
            do_read();
            return;
        }

        std::string_view raw { reinterpret_cast<const char*>(raw_data), raw_len };

        try {
            auto msg = json::parse(raw);
            msg["from"] = id_;

            std::string type = msg.value("type", "");
            if (type == "streaming_start") {
                server_->add_streaming_peer(id_, room_id_);
            } else if (type == "streaming_stop") {
                server_->remove_streaming_peer(id_, room_id_);
            }

            if (msg.contains("to")) {
                std::string to = msg["to"];
                server_->send_to(to, room_id_, msg.dump());
            } else {
                server_->broadcast(id_, room_id_, msg.dump());
            }
        } catch (const json::exception& e) {
            LOG_ERROR() << "json parse error [" << id_ << "]: " << e.what();
        }

        buffer_.consume(buffer_.size());
        do_read();
    }

    void on_close()
    {
        server_->unregister_session(id_, room_id_);
        LOG_INFO() << "session " << id_ << " disconnected (room=" << room_id_ << ")";
    }

    beast::flat_buffer buffer_;
    websocket::stream<beast::tcp_stream> ws_;
    std::string id_;
    std::string room_id_;
    std::string username_;
    std::shared_ptr<WebSocketServer> server_;
    std::mutex write_mutex_;
    std::deque<OutboundMessage> write_queue_;
};

// --- WebSocketServer ---------------------------------------------------------

WebSocketServer::WebSocketServer(boost::asio::io_context& io_context,
    unsigned short port)
    : io_context_(io_context)
    , acceptor_(io_context_, tcp::endpoint(tcp::v4(), port))
{
}

void WebSocketServer::run()
{
    do_accept();
}

void WebSocketServer::stop()
{
    boost::system::error_code ec;
    acceptor_.close(ec);

    std::scoped_lock lk(rooms_mutex_);
    rooms_.clear();
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

std::string WebSocketServer::register_and_build_welcome(const std::string& id,
    const std::string& room_id,
    std::shared_ptr<Session> s)
{
    std::vector<std::shared_ptr<Session>> existing;
    std::string welcome_payload;
    {
        std::scoped_lock lk(rooms_mutex_);

        auto& room = rooms_[room_id];

        json welcome;
        welcome["type"] = "welcome";
        welcome["id"] = id;
        json peers = json::array();
        existing.reserve(room.sessions.size());
        for (auto& [pid, session] : room.sessions) {
            peers.push_back(pid);
            existing.push_back(session);
        }
        welcome["peers"] = std::move(peers);

        json streaming = json::array();
        for (auto& sid : room.streaming_peers) {
            streaming.push_back(sid);
        }
        welcome["streaming_peers"] = std::move(streaming);

        welcome_payload = welcome.dump();

        room.sessions.emplace(id, std::move(s));
    }

    json joined;
    joined["type"] = "peer_joined";
    joined["id"] = id;
    auto joined_msg = std::make_shared<std::string>(joined.dump());
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
                { "id", pid },
                { "username", session ? session->username() : "" },
            });
        }
        out[room_id] = std::move(arr);
    }
    return out.dump();
}

std::string WebSocketServer::media_stats_json() const
{
    json out = json::object();
    std::scoped_lock lk(rooms_mutex_);
    for (const auto& [room_id, room] : rooms_) {
        out[room_id] = {
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

void WebSocketServer::unregister_session(const std::string& id,
    const std::string& room_id)
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
        remaining.reserve(room.sessions.size());
        for (auto& [_, session] : room.sessions) {
            remaining.push_back(session);
        }
        // Remove empty rooms to avoid unbounded map growth.
        if (room.sessions.empty()) {
            rooms_.erase(rit);
        }
    }

    json left;
    left["type"] = "peer_left";
    left["id"] = id;
    auto msg = std::make_shared<std::string>(left.dump());
    for (auto& session : remaining) {
        session->send(msg);
    }
}

void WebSocketServer::broadcast(const std::string& from_id,
    const std::string& room_id,
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

void WebSocketServer::send_to(const std::string& target_id,
    const std::string& room_id,
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

void WebSocketServer::relay_binary_media(const std::string& from_id,
    const std::string& room_id,
    const uint8_t* data,
    size_t len)
{
    MediaClientHeader header;
    if (!parse_media_client_header(data, len, header)) {
        std::scoped_lock lk(rooms_mutex_);
        rooms_[room_id].media_packets_dropped++;
        LOG_WARNING() << "dropping invalid media relay packet from " << from_id
                      << " in room " << room_id << " (" << len << " bytes)";
        return;
    }

    const uint8_t* payload = data + kMediaClientHeaderSize;
    const size_t payload_len = len - kMediaClientHeaderSize;
    auto relay = std::make_shared<std::string>(
        build_media_relay_packet(from_id, header.kind, payload, payload_len));

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
        targets.reserve(room.sessions.size());
        for (auto& [pid, session] : room.sessions) {
            if (pid != from_id) {
                targets.push_back(session);
            }
        }
        room.media_packets_out += targets.size();
        room.media_bytes_out += relay->size() * targets.size();
    }

    for (auto& session : targets) {
        session->send_binary(relay);
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

size_t WebSocketServer::active_sessions(const std::string& room_id) const
{
    std::scoped_lock lk(rooms_mutex_);
    auto it = rooms_.find(room_id);
    return it != rooms_.end() ? it->second.sessions.size() : 0;
}

void WebSocketServer::add_streaming_peer(const std::string& id,
    const std::string& room_id)
{
    std::scoped_lock lk(rooms_mutex_);
    rooms_[room_id].streaming_peers.insert(id);
}

void WebSocketServer::remove_streaming_peer(const std::string& id,
    const std::string& room_id)
{
    std::scoped_lock lk(rooms_mutex_);
    auto rit = rooms_.find(room_id);
    if (rit != rooms_.end()) {
        rit->second.streaming_peers.erase(id);
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
            if (self->acceptor_.is_open()) {
                self->do_accept();
            }
        });
}

} // namespace driscord
