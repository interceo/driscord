#include "ws_server.hpp"

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

    void send(std::shared_ptr<std::string> msg)
    {
        if (write_queue_.size() >= kMaxWriteQueueSize) {
            LOG_WARNING() << "write queue overflow for " << id_
                          << ", dropping message";
            return;
        }
        write_queue_.push_back(std::move(msg));
        if (write_queue_.size() == 1) {
            do_write();
        }
    }

private:
    void do_write()
    {
        ws_.text(true);
        ws_.async_write(
            boost::asio::buffer(*write_queue_.front()),
            beast::bind_front_handler(&Session::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t)
    {
        if (ec) {
            LOG_ERROR() << "write error [" << id_ << "]: " << ec.message();
            return;
        }
        write_queue_.pop_front();
        if (!write_queue_.empty()) {
            do_write();
        }
    }

    void on_accept(beast::error_code ec)
    {
        if (ec) {
            LOG_ERROR() << "accept: " << ec.message();
            return;
        }

        auto welcome = server_->build_welcome(id_);
        send(std::make_shared<std::string>(std::move(welcome)));

        server_->register_session(id_, shared_from_this());

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

void WebSocketServer::register_session(const std::string& id,
    std::shared_ptr<Session> s)
{
    std::scoped_lock lk(sessions_mutex_);

    json joined;
    joined["type"] = "peer_joined";
    joined["id"] = id;
    auto msg = std::make_shared<std::string>(joined.dump());
    for (auto& [_, session] : sessions_) {
        session->send(msg);
    }

    sessions_.emplace(id, std::move(s));
}

void WebSocketServer::unregister_session(const std::string& id)
{
    std::scoped_lock lk(sessions_mutex_);
    sessions_.erase(id);
    streaming_peers_.erase(id);

    json left;
    left["type"] = "peer_left";
    left["id"] = id;
    auto msg = std::make_shared<std::string>(left.dump());
    for (auto& [_, session] : sessions_) {
        session->send(msg);
    }
}

std::string WebSocketServer::build_welcome(const std::string& new_id)
{
    std::scoped_lock lk(sessions_mutex_);
    json welcome;
    welcome["type"] = "welcome";
    welcome["id"] = new_id;
    json peers = json::array();
    for (auto& [pid, _] : sessions_) {
        peers.push_back(pid);
    }
    welcome["peers"] = peers;
    json streaming = json::array();
    for (auto& sid : streaming_peers_) {
        streaming.push_back(sid);
    }
    welcome["streaming_peers"] = streaming;
    return welcome.dump();
}

void WebSocketServer::broadcast(const std::string& from_id,
    const std::string& msg)
{
    std::scoped_lock lk(sessions_mutex_);
    auto shared_msg = std::make_shared<std::string>(msg);
    for (auto& [pid, session] : sessions_) {
        if (pid != from_id) {
            session->send(shared_msg);
        }
    }
}

void WebSocketServer::send_to(const std::string& target_id,
    const std::string& msg)
{
    std::scoped_lock lk(sessions_mutex_);
    auto it = sessions_.find(target_id);
    if (it != sessions_.end()) {
        it->second->send(std::make_shared<std::string>(msg));
    }
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
