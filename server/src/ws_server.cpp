#include "ws_server.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <unordered_set>
#include <mutex>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

namespace driscord {

namespace {
struct Session : public std::enable_shared_from_this<Session> {
    websocket::stream<beast::tcp_stream> ws;
    beast::flat_buffer buffer;
    std::mutex* broadcast_mutex;
    std::unordered_set<Session*>* sessions;

    Session(tcp::socket&& socket,
            std::mutex& bmutex,
            std::unordered_set<Session*>& all)
        : ws(std::move(socket)), broadcast_mutex(&bmutex), sessions(&all) {}

    void start() {
        ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(http::field::server, std::string("driscord/ws"));
            }));
        // Accept the websocket handshake
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
            sessions->insert(this);
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

        // Broadcast the message to all sessions (naive signaling bus)
        std::string_view msg{static_cast<const char*>(buffer.data().data()), buffer.data().size()};
        {
            std::scoped_lock lk(*broadcast_mutex);
            for (auto* s : *sessions) {
                if (s == this) continue;
                s->ws.text(ws.got_text());
                s->ws.async_write(
                    boost::asio::buffer(msg),
                    [self = s->shared_from_this()](beast::error_code, std::size_t) {});
            }
        }
        buffer.consume(buffer.size());
        do_read();
    }

    void on_close() {
        std::scoped_lock lk(*broadcast_mutex);
        sessions->erase(this);
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
                static std::unordered_set<Session*> sessions;
                std::make_shared<Session>(std::move(socket), bmutex, sessions)->start();
            }
            do_accept();
        });
}

} // namespace driscord 