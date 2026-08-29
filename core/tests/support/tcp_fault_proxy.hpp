#pragma once

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace test_util {

struct TcpFaultConfig {
    bool refuse = false;
    std::chrono::milliseconds drop_after { 0 };
    std::chrono::milliseconds latency { 0 };
};

class TcpFaultProxy {
public:
    using Config = TcpFaultConfig;

    TcpFaultProxy(const std::string& upstream_host,
        unsigned short upstream_port,
        Config config = { })
        : upstream_host_(upstream_host)
        , upstream_port_(upstream_port)
        , config_(config)
        , acceptor_(io_,
              boost::asio::ip::tcp::endpoint(
                  boost::asio::ip::make_address("127.0.0.1"), 0))
    {
        port_ = acceptor_.local_endpoint().port();
        accept();
        work_.emplace(boost::asio::make_work_guard(io_));
        thread_ = std::thread([this] { io_.run(); });
    }

    ~TcpFaultProxy()
    {
        boost::system::error_code ignored;
        acceptor_.close(ignored);
        work_.reset();
        io_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    TcpFaultProxy(const TcpFaultProxy&) = delete;
    TcpFaultProxy& operator=(const TcpFaultProxy&) = delete;

    unsigned short port() const { return port_; }

    void sever_connections() { severed_.store(true); }

private:
    using tcp = boost::asio::ip::tcp;

    struct Bridge : std::enable_shared_from_this<Bridge> {
        Bridge(boost::asio::io_context& io, tcp::socket client, Config config,
            const std::atomic<bool>& severed)
            : client_(std::move(client))
            , upstream_(io)
            , timer_(io)
            , config_(config)
            , severed_(severed)
        {
        }

        void run(const std::string& host, unsigned short port)
        {
            auto self = shared_from_this();
            tcp::resolver resolver(upstream_.get_executor());
            boost::system::error_code ec;
            const auto endpoints
                = resolver.resolve(host, std::to_string(port), ec);
            if (ec) {
                return;
            }
            boost::asio::async_connect(upstream_, endpoints,
                [this, self](boost::system::error_code connect_ec,
                    const tcp::endpoint&) {
                    if (connect_ec) {
                        return;
                    }
                    if (config_.drop_after.count() > 0) {
                        timer_.expires_after(config_.drop_after);
                        timer_.async_wait([this, self](boost::system::error_code) {
                            close();
                        });
                    }
                    pump(client_, upstream_, to_upstream_, true);
                    pump(upstream_, client_, to_client_, false);
                });
        }

        void pump(tcp::socket& from, tcp::socket& to,
            std::array<char, 16384>& buffer, bool first_in_direction)
        {
            auto self = shared_from_this();
            from.async_read_some(boost::asio::buffer(buffer),
                [this, self, &from, &to, &buffer, first_in_direction](
                    boost::system::error_code ec, std::size_t n) {
                    if (ec || severed_.load()) {
                        close();
                        return;
                    }
                    const auto forward = [this, self, &from, &to, &buffer,
                                             first_in_direction, n] {
                        boost::asio::async_write(to,
                            boost::asio::buffer(buffer, n),
                            [this, self, &from, &to, &buffer,
                                first_in_direction](
                                boost::system::error_code write_ec,
                                std::size_t) {
                                if (write_ec) {
                                    close();
                                    return;
                                }
                                pump(from, to, buffer, false);
                                (void)first_in_direction;
                            });
                    };
                    if (first_in_direction && config_.latency.count() > 0) {
                        timer_.expires_after(config_.latency);
                        timer_.async_wait(
                            [forward](boost::system::error_code) { forward(); });
                    } else {
                        forward();
                    }
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
        boost::asio::steady_timer timer_;
        Config config_;
        const std::atomic<bool>& severed_;
        std::array<char, 16384> to_upstream_ { };
        std::array<char, 16384> to_client_ { };
    };

    void accept()
    {
        acceptor_.async_accept([this](boost::system::error_code ec,
                                   tcp::socket socket) {
            if (ec) {
                return;
            }
            if (!config_.refuse) {
                auto bridge = std::make_shared<Bridge>(
                    io_, std::move(socket), config_, severed_);
                bridge->run(upstream_host_, upstream_port_);
            }
            accept();
        });
    }

    boost::asio::io_context io_;
    std::string upstream_host_;
    unsigned short upstream_port_;
    Config config_;
    tcp::acceptor acceptor_;
    std::atomic<bool> severed_ { false };
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;
    std::thread thread_;
    unsigned short port_ { 0 };
};

}
