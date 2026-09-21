#pragma once

#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace test_util {

// The smallest HTTP CONNECT proxy that libdatachannel's WebSocket tunnel is
// happy with. It exists so a test can prove signaling really traversed a proxy
// instead of merely tolerating the setting: every CONNECT target is recorded,
// so an assertion can name the host the client asked for.
//
// Everything is asynchronous on one io_context thread, like TcpFaultProxy next
// to it. A blocking accept() cannot be woken reliably by closing the acceptor
// from another thread, and a test that hangs on teardown is worse than any
// bug it could catch.
class HttpConnectProxy {
public:
    HttpConnectProxy()
        : acceptor_(io_,
              boost::asio::ip::tcp::endpoint(
                  boost::asio::ip::make_address("127.0.0.1"), 0))
    {
        port_ = acceptor_.local_endpoint().port();
        accept();
        work_.emplace(boost::asio::make_work_guard(io_));
        thread_ = std::thread([this] { io_.run(); });
    }

    ~HttpConnectProxy()
    {
        boost::system::error_code ignored;
        acceptor_.close(ignored);
        work_.reset();
        io_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    HttpConnectProxy(const HttpConnectProxy&) = delete;
    HttpConnectProxy& operator=(const HttpConnectProxy&) = delete;

    unsigned short port() const { return port_; }

    // "host:port" for every CONNECT request seen, in arrival order.
    std::vector<std::string> targets() const
    {
        std::scoped_lock lock(mutex_);
        return targets_;
    }

private:
    using tcp = boost::asio::ip::tcp;

    struct Bridge : std::enable_shared_from_this<Bridge> {
        Bridge(boost::asio::io_context& io, tcp::socket client,
            HttpConnectProxy& owner)
            : client_(std::move(client))
            , upstream_(io)
            , resolver_(io)
            , owner_(owner)
        {
        }

        void run()
        {
            auto self = shared_from_this();
            boost::asio::async_read_until(client_, request_, "\r\n\r\n",
                [this, self](boost::system::error_code ec, std::size_t) {
                    if (ec) {
                        close();
                        return;
                    }
                    on_request();
                });
        }

        void on_request()
        {
            const std::string text(boost::asio::buffers_begin(request_.data()),
                boost::asio::buffers_end(request_.data()));
            const auto line_end = text.find("\r\n");
            if (line_end == std::string::npos
                || text.rfind("CONNECT ", 0) != 0) {
                close();
                return;
            }
            const std::string line = text.substr(0, line_end);
            const auto target_end = line.find(' ', 8);
            if (target_end == std::string::npos) {
                close();
                return;
            }
            const std::string target = line.substr(8, target_end - 8);
            const auto colon = target.rfind(':');
            if (colon == std::string::npos) {
                close();
                return;
            }
            owner_.record(target);

            auto self = shared_from_this();
            resolver_.async_resolve(target.substr(0, colon),
                target.substr(colon + 1),
                [this, self](boost::system::error_code ec,
                    const tcp::resolver::results_type& endpoints) {
                    if (ec) {
                        close();
                        return;
                    }
                    boost::asio::async_connect(upstream_, endpoints,
                        [this, self](boost::system::error_code connect_ec,
                            const tcp::endpoint&) {
                            if (connect_ec) {
                                close();
                                return;
                            }
                            establish();
                        });
                });
        }

        void establish()
        {
            static constexpr std::string_view kEstablished
                = "HTTP/1.1 200 Connection established\r\n\r\n";
            auto self = shared_from_this();
            boost::asio::async_write(client_,
                boost::asio::buffer(kEstablished.data(), kEstablished.size()),
                [this, self](boost::system::error_code ec, std::size_t) {
                    if (ec) {
                        close();
                        return;
                    }
                    pump(client_, upstream_, to_upstream_);
                    pump(upstream_, client_, to_client_);
                });
        }

        void pump(tcp::socket& from, tcp::socket& to,
            std::array<char, 16384>& buffer)
        {
            auto self = shared_from_this();
            from.async_read_some(boost::asio::buffer(buffer),
                [this, self, &from, &to, &buffer](
                    boost::system::error_code ec, std::size_t n) {
                    if (ec) {
                        close();
                        return;
                    }
                    boost::asio::async_write(to, boost::asio::buffer(buffer, n),
                        [this, self, &from, &to, &buffer](
                            boost::system::error_code write_ec, std::size_t) {
                            if (write_ec) {
                                close();
                                return;
                            }
                            pump(from, to, buffer);
                        });
                });
        }

        void close()
        {
            boost::system::error_code ignored;
            client_.close(ignored);
            upstream_.close(ignored);
        }

        tcp::socket client_;
        tcp::socket upstream_;
        tcp::resolver resolver_;
        HttpConnectProxy& owner_;
        boost::asio::streambuf request_;
        std::array<char, 16384> to_upstream_ { };
        std::array<char, 16384> to_client_ { };
    };

    void record(const std::string& target)
    {
        std::scoped_lock lock(mutex_);
        targets_.push_back(target);
    }

    void accept()
    {
        acceptor_.async_accept([this](boost::system::error_code ec,
                                   tcp::socket socket) {
            if (ec) {
                return;
            }
            std::make_shared<Bridge>(io_, std::move(socket), *this)->run();
            accept();
        });
    }

    boost::asio::io_context io_;
    tcp::acceptor acceptor_;
    unsigned short port_ { 0 };
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;
    std::thread thread_;

    mutable std::mutex mutex_;
    std::vector<std::string> targets_;
};

}
