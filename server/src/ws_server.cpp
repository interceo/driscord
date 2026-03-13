#include "ws_server.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

namespace driscord {

namespace {

std::string generate_id() {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << dist(rng);
    return ss.str();
}

struct Session : public std::enable_shared_from_this<Session> {
    websocket::stream<beast::tcp_stream> ws;
    beast::flat_buffer buffer;
    std::string id;
    std::mutex* broadcast_mutex;
    std::unordered_map<std::string, Session*>* sessions;

    Session(tcp::socket&& socket,
            std::mutex& bmutex,
            std::unordered_map<std::string, Session*>& all)
        : ws(std::move(socket))
        , id(generate_id())
        , broadcast_mutex(&bmutex)
        , sessions(&all) {}

    void start() {
        ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(http::field::server, "driscord/ws");
            }));
        ws.async_accept(
            beast::bind_front_handler(&Session::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "accept: " << ec.message() << std::endl;
            return;
        }

        {
            std::scoped_lock lk(*broadcast_mutex);

            json welcome;
            welcome["type"] = "welcome";
            welcome["id"] = id;

            json peers = json::array();
            for (auto& [pid, _] : *sessions) {
                peers.push_back(pid);
            }
            welcome["peers"] = peers;

            auto msg = welcome.dump();
            ws.text(true);
            ws.write(boost::asio::buffer(msg));

            json joined;
            joined["type"] = "peer_joined";
            joined["id"] = id;
            auto joined_msg = joined.dump();
            for (auto& [_, s] : *sessions) {
                s->ws.text(true);
                s->ws.async_write(
                    boost::asio::buffer(joined_msg),
                    [self = s->shared_from_this()](beast::error_code, std::size_t) {});
            }

            sessions->emplace(id, this);
        }

        do_read();
    }

    void do_read() {
        ws.async_read(buffer, beast::bind_front_handler(&Session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t) {
        if (ec == websocket::error::closed) {
            on_close();
            return;
        }
        if (ec) {
            std::cerr << "read: " << ec.message() << std::endl;
            on_close();
            return;
        }

        std::string_view raw{static_cast<const char*>(buffer.data().data()), buffer.data().size()};

        try {
            auto msg = json::parse(raw);
            msg["from"] = id;

            if (msg.contains("to")) {
                std::string to = msg["to"];
                auto fwd = msg.dump();
                std::scoped_lock lk(*broadcast_mutex);
                auto it = sessions->find(to);
                if (it != sessions->end()) {
                    it->second->ws.text(true);
                    it->second->ws.async_write(
                        boost::asio::buffer(fwd),
                        [self = it->second->shared_from_this()](beast::error_code, std::size_t) {});
                }
            } else {
                auto fwd = msg.dump();
                std::scoped_lock lk(*broadcast_mutex);
                for (auto& [pid, s] : *sessions) {
                    if (pid == id) continue;
                    s->ws.text(true);
                    s->ws.async_write(
                        boost::asio::buffer(fwd),
                        [self = s->shared_from_this()](beast::error_code, std::size_t) {});
                }
            }
        } catch (const json::exception& e) {
            std::cerr << "json parse error: " << e.what() << std::endl;
        }

        buffer.consume(buffer.size());
        do_read();
    }

    void on_close() {
        std::scoped_lock lk(*broadcast_mutex);
        sessions->erase(id);

        json left;
        left["type"] = "peer_left";
        left["id"] = id;
        auto msg = left.dump();
        for (auto& [_, s] : *sessions) {
            s->ws.text(true);
            s->ws.async_write(
                boost::asio::buffer(msg),
                [self = s->shared_from_this()](beast::error_code, std::size_t) {});
        }
    }
};

} // namespace

WebSocketServer::WebSocketServer(boost::asio::io_context& io_context, unsigned short port)
    : io_context_(io_context),
      acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) {}

void WebSocketServer::run() { do_accept(); }

void WebSocketServer::do_accept() {
    acceptor_.async_accept(
        boost::asio::make_strand(io_context_),
        [this](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                static std::mutex bmutex;
                static std::unordered_map<std::string, Session*> sessions;
                std::make_shared<Session>(std::move(socket), bmutex, sessions)->start();
            }
            do_accept();
        });
}

} // namespace driscord
