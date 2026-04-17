#include "ws_server.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <deque>
#include <iomanip>
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

constexpr size_t kMaxMessageSize = 64 * 1024;
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

    const std::string& id() const { return id_; }
    const std::string& room_id() const { return room_id_; }

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

private:
    // Called after we've read the HTTP upgrade request. Extracts the room_id
    // from the request target, then completes the WebSocket handshake.
    void on_http_read(std::shared_ptr<http::request<http::string_body>> req,
        beast::error_code ec,
        std::size_t)
    {
        if (ec) {
            LOG_ERROR() << "http read [" << id_ << "]: " << ec.message();
            return;
        }
        room_id_ = parse_room_id(req->target());
        LOG_INFO() << "session " << id_ << " joining room " << room_id_;

        ws_.async_accept(*req,
            beast::bind_front_handler(&Session::on_accept, shared_from_this()));
    }

    // Runs on the session's strand.
    void do_write()
    {
        std::shared_ptr<std::string> msg;
        {
            std::scoped_lock lk(write_mutex_);
            if (write_queue_.empty()) {
                return;
            }
            msg = write_queue_.front();
        }
        ws_.text(true);
        ws_.async_write(
            boost::asio::buffer(*msg),
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

        std::string_view raw { static_cast<const char*>(buffer_.data().data()),
            buffer_.data().size() };

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
    std::shared_ptr<WebSocketServer> server_;
    std::mutex write_mutex_;
    std::deque<std::shared_ptr<std::string>> write_queue_;
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
