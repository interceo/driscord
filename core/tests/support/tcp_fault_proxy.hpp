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

// A one-hop TCP fault proxy for the connection-oriented paths WebRTC's
// UDP-only network emulation cannot reach: the WSS signaling socket and the
// REST API. It listens on an ephemeral port and forwards to a fixed upstream,
// applying the impairments a test configures. Same idea as Toxiproxy / Pion's
// vnet — a single injection point in front of the socket — but in-process, no
// external binary, no privileges.
//
// The impairment set is intentionally small: reject the connection outright,
// drop it after a while, or add latency to the first bytes each way. That is
// enough to exercise reconnect, handshake-timeout and slow-API behaviour.
// Impairments applied to a proxied connection. Namespace-scoped so it can be
// a default constructor argument.
struct TcpFaultConfig {
    // Refuse every connection at accept time (models a down upstream).
    bool refuse = false;
    // Close the connection this long after it opens (0 = never).
    std::chrono::milliseconds drop_after { 0 };
    // Delay applied before forwarding the first chunk in each direction.
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

    // A test flips this to sever every live and future connection, modelling
    // the upstream vanishing mid-session.
    void sever_connections() { severed_.store(true); }

private:
    using tcp = boost::asio::ip::tcp;

    // One proxied connection: a downstream client socket and an upstream
    // server socket, pumping bytes both ways until either side closes.
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
            // config_.refuse just drops the accepted socket → RST/close.
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

} // namespace test_util
