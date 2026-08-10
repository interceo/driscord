#pragma once

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace test_util {

// Minimal stand-in for the REST API used by the signaling server's session
// authorization. It records what was asked and replies with whatever the test
// configured, so authorization can be exercised without a database.
class FakeApiServer {
public:
    struct Request {
        std::string target;
        std::string authorization;
    };

    FakeApiServer()
        : acceptor_(io_,
              boost::asio::ip::tcp::endpoint(
                  boost::asio::ip::make_address("127.0.0.1"), 0))
    {
        accept();
        work_.emplace(boost::asio::make_work_guard(io_));
        thread_ = std::thread([this] { io_.run(); });
    }

    ~FakeApiServer()
    {
        boost::system::error_code ignored;
        acceptor_.close(ignored);
        work_.reset();
        io_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    FakeApiServer(const FakeApiServer&) = delete;
    FakeApiServer& operator=(const FakeApiServer&) = delete;

    std::string base_url() const
    {
        return "http://127.0.0.1:"
            + std::to_string(acceptor_.local_endpoint().port());
    }

    void set_response(unsigned status, std::string body)
    {
        std::scoped_lock lock(mutex_);
        status_ = status;
        body_ = std::move(body);
    }

    std::vector<Request> requests() const
    {
        std::scoped_lock lock(mutex_);
        return requests_;
    }

private:
    void accept()
    {
        acceptor_.async_accept([this](boost::system::error_code ec,
                                   boost::asio::ip::tcp::socket socket) {
            if (ec) {
                return;
            }
            serve(std::make_shared<boost::asio::ip::tcp::socket>(
                std::move(socket)));
            accept();
        });
    }

    void serve(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
    {
        auto buffer = std::make_shared<boost::beast::flat_buffer>();
        auto request = std::make_shared<
            boost::beast::http::request<boost::beast::http::string_body>>();
        boost::beast::http::async_read(*socket, *buffer, *request,
            [this, socket, buffer, request](
                boost::system::error_code ec, std::size_t) {
                if (ec) {
                    return;
                }
                unsigned status = 0;
                std::string body;
                {
                    std::scoped_lock lock(mutex_);
                    requests_.push_back(Request {
                        std::string(request->target()),
                        std::string(
                            (*request)[boost::beast::http::field::authorization]),
                    });
                    status = status_;
                    body = body_;
                }
                auto response = std::make_shared<boost::beast::http::response<
                    boost::beast::http::string_body>>();
                response->version(request->version());
                response->result(status);
                response->set(boost::beast::http::field::content_type,
                    "application/json");
                response->keep_alive(false);
                response->body() = std::move(body);
                response->prepare_payload();
                boost::beast::http::async_write(*socket, *response,
                    [socket, response](boost::system::error_code, std::size_t) {
                        boost::system::error_code ignored;
                        socket->shutdown(
                            boost::asio::ip::tcp::socket::shutdown_both,
                            ignored);
                    });
            });
    }

    boost::asio::io_context io_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;
    std::thread thread_;
    mutable std::mutex mutex_;
    unsigned status_ = 200;
    std::string body_ = R"({"user_id": 1, "username": "tester"})";
    std::vector<Request> requests_;
};

} // namespace test_util
