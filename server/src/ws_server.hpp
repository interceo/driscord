#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace driscord {

class Session;

class WebSocketServer : public std::enable_shared_from_this<WebSocketServer> {
public:
    explicit WebSocketServer(boost::asio::io_context& io_context, unsigned short port);

    void run();
    void stop();

    void register_session(const std::string& id, std::shared_ptr<Session> s);
    void unregister_session(const std::string& id);
    std::string build_welcome(const std::string& new_id);
    void broadcast(const std::string& from_id, const std::string& msg);
    void send_to(const std::string& target_id, const std::string& msg);

private:
    void do_accept();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;

    mutable std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
};

}  // namespace driscord
