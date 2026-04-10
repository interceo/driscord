#include "ws_server.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <deque>
#include <iomanip>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
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

    void start()
    {
        ws_.set_option(
            websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.set_option(
            websocket::stream_base::decorator([](websocket::response_type& res) {
                res.set(http::field::server, "driscord/ws");
            }));
        ws_.read_message_max(kMaxMessageSize);
        ws_.async_accept(
            beast::bind_front_handler(&Session::on_accept, shared_from_this()));
    }

    // send() may be called from any thread (notably from other sessions'
    // strands via WebSocketServer::broadcast / send_to). It is therefore
    // protected by write_mutex_ and posts the actual write onto this
    // session's strand so that ws_ is only ever touched from one place.
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
            LOG_ERROR() << "accept: " << ec.message();
            return;
        }

        // Register atomically: snapshot existing sessions, insert self,
        // broadcast peer_joined, and return the welcome payload — all under
        // one critical section. This removes the old race window between
        // build_welcome and register_session where two concurrent joiners
        // could miss each other.
        auto welcome = server_->register_and_build_welcome(id_,
            shared_from_this());
        send(std::make_shared<std::string>(std::move(welcome)));

        LOG_INFO() << "session " << id_ << " connected";
        do_read();
    }

    void do_read()
    {
        ws_.async_read(buffer_, beast::bind_front_handler(&Session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t)
    {
        if (ec == websocket::error::closed) {
            on_close();
            return;
        }
        if (ec) {
            LOG_ERROR() << "read: " << ec.message();
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
                server_->add_streaming_peer(id_);
            } else if (type == "streaming_stop") {
                server_->remove_streaming_peer(id_);
            }

            if (msg.contains("to")) {
                std::string to = msg["to"];
                server_->send_to(to, msg.dump());
            } else {
                server_->broadcast(id_, msg.dump());
            }
        } catch (const json::exception& e) {
            LOG_ERROR() << "json parse error: " << e.what();
        }

        buffer_.consume(buffer_.size());
        do_read();
    }

    void on_close()
    {
        server_->unregister_session(id_);
        LOG_INFO() << "session " << id_ << " disconnected";
    }

    beast::flat_buffer buffer_;
    websocket::stream<beast::tcp_stream> ws_;
    std::string id_;
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

    std::scoped_lock lk(sessions_mutex_);
    sessions_.clear();
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
    std::shared_ptr<Session> s)
{
    std::vector<std::shared_ptr<Session>> existing;
    std::string welcome_payload;
    {
        std::scoped_lock lk(sessions_mutex_);

        // Snapshot the existing peer IDs and streaming set while holding the
        // lock — this becomes the welcome payload for the new session.
        json welcome;
        welcome["type"] = "welcome";
        welcome["id"] = id;
        json peers = json::array();
        existing.reserve(sessions_.size());
        for (auto& [pid, session] : sessions_) {
            peers.push_back(pid);
            existing.push_back(session);
        }
        welcome["peers"] = std::move(peers);

        json streaming = json::array();
        for (auto& sid : streaming_peers_) {
            streaming.push_back(sid);
        }
        welcome["streaming_peers"] = std::move(streaming);

        welcome_payload = welcome.dump();

        // Insert the new session under the SAME lock, so any other joiner
        // that acquires the lock next sees both our snapshot and our
        // presence consistently.
        sessions_.emplace(id, std::move(s));
    }

    // Broadcast peer_joined to the previously-existing sessions OUTSIDE the
    // lock. The new session does not receive its own peer_joined (it is not
    // in `existing`). Calling Session::send here is safe because Session
    // now uses its own write_mutex_ and posts writes to its own strand.
    json joined;
    joined["type"] = "peer_joined";
    joined["id"] = id;
    auto joined_msg = std::make_shared<std::string>(joined.dump());
    for (auto& session : existing) {
        session->send(joined_msg);
    }

    return welcome_payload;
}

void WebSocketServer::unregister_session(const std::string& id)
{
    std::vector<std::shared_ptr<Session>> remaining;
    {
        std::scoped_lock lk(sessions_mutex_);
        sessions_.erase(id);
        streaming_peers_.erase(id);
        remaining.reserve(sessions_.size());
        for (auto& [_, session] : sessions_) {
            remaining.push_back(session);
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
    const std::string& msg)
{
    std::vector<std::shared_ptr<Session>> targets;
    {
        std::scoped_lock lk(sessions_mutex_);
        targets.reserve(sessions_.size());
        for (auto& [pid, session] : sessions_) {
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
    const std::string& msg)
{
    std::shared_ptr<Session> target;
    {
        std::scoped_lock lk(sessions_mutex_);
        auto it = sessions_.find(target_id);
        if (it != sessions_.end()) {
            target = it->second;
        }
    }
    if (target) {
        target->send(std::make_shared<std::string>(msg));
    }
}

size_t WebSocketServer::active_sessions() const
{
    std::scoped_lock lk(sessions_mutex_);
    return sessions_.size();
}

void WebSocketServer::add_streaming_peer(const std::string& id)
{
    std::scoped_lock lk(sessions_mutex_);
    streaming_peers_.insert(id);
}

void WebSocketServer::remove_streaming_peer(const std::string& id)
{
    std::scoped_lock lk(sessions_mutex_);
    streaming_peers_.erase(id);
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
