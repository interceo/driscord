
#include "signaling_test_fixture.hpp"
#include "utils/log.hpp"
#include "wait_helpers.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using test_util::SignalingServerFixture;

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
const SuppressLogs suppress_logs_on_startup;

class RawWsClient {
public:
    RawWsClient(unsigned short port, const std::string& target)
        : socket_(io_)
        , ws_(socket_)
    {
        socket_.connect(
            tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port));
        ws_.handshake("127.0.0.1", target);
    }

    void send(const std::string& text) { ws_.write(boost::asio::buffer(text)); }

    std::string read()
    {
        beast::flat_buffer buffer;
        ws_.read(buffer);
        return beast::buffers_to_string(buffer.data());
    }

    std::string read_until(const std::string& needle)
    {
        try {
            for (int i = 0; i < 64; ++i) {
                auto message = read();
                if (message.find(needle) != std::string::npos) {
                    return message;
                }
            }
        } catch (const std::exception&) {
        }
        return { };
    }

    void abort()
    {
        boost::system::error_code ignored;
        socket_.set_option(boost::asio::socket_base::linger(true, 0), ignored);
        socket_.close(ignored);
    }

private:
    boost::asio::io_context io_;
    tcp::socket socket_;
    websocket::stream<tcp::socket&> ws_;
};

std::string ws_target(int channel, const std::string& username)
{
    return "/channels/" + std::to_string(channel) + "?u=" + username;
}

std::string offer_with_sdp(const std::string& sdp, const char* connection)
{
    const nlohmann::json message {
        { "type", "offer" },
        { "sdp", sdp },
        { "connection", connection },
    };
    return message.dump();
}

class WsServerHostileTest : public ::testing::Test {
protected:
    SignalingServerFixture fixture_;
};

TEST_F(WsServerHostileTest, GarbageFramesDoNotKillTheSession)
{
    RawWsClient alice(fixture_.port(), ws_target(1, "alice"));
    ASSERT_FALSE(alice.read_until("welcome").empty());

    const std::vector<std::string> garbage {
        "definitely not json",
        R"({"type":"offer","sdp":)",
        R"({"type":"bogus_message_type"})",
        R"({"type":"offer","sdp":"v=0"})",
        R"({"type":"offer","sdp":"v=0","connection":"legacy"})",
        "{}",
        "[]",
        "null",
        std::string(1, '\0') + "binary-ish",
    };
    for (const auto& frame : garbage) {
        alice.send(frame);
    }

    RawWsClient bob(fixture_.port(), ws_target(1, "bob"));
    ASSERT_FALSE(bob.read_until("welcome").empty());
    EXPECT_FALSE(alice.read_until("peer_joined").empty())
        << "the session that received garbage stopped receiving broadcasts";
    EXPECT_EQ(fixture_.active_sessions(), 2u);
}

TEST_F(WsServerHostileTest, OversizedFrameIsSurvivable)
{
    RawWsClient alice(fixture_.port(), ws_target(1, "alice"));
    ASSERT_FALSE(alice.read_until("welcome").empty());

    std::string huge = R"({"type":"offer","sdp":")";
    huge.append(8 * 1024 * 1024, 'a');
    huge += R"(","connection":"voice"})";
    try {
        alice.send(huge);
    } catch (const std::exception&) {
    }

    RawWsClient bob(fixture_.port(), ws_target(1, "bob"));
    EXPECT_FALSE(bob.read_until("welcome").empty());
}

TEST_F(WsServerHostileTest, HostileUsernamesAreEscapedEverywhere)
{
    const std::vector<std::string> hostile {
        "quote%22inject",
        "back%5Cslash",
        "ctrl%01%02%1F",
        "%3Cscript%3Ealert(1)%3C%2Fscript%3E",
        std::string(2048, 'x'),
    };
    std::vector<std::unique_ptr<RawWsClient>> clients;
    for (const auto& name : hostile) {
        clients.push_back(
            std::make_unique<RawWsClient>(fixture_.port(), ws_target(1, name)));
        ASSERT_FALSE(clients.back()->read_until("welcome").empty());
    }

    const auto [status, body] = fixture_.http_get("/presence");
    ASSERT_EQ(status, 200u);
    const auto presence = nlohmann::json::parse(body, nullptr, false);
    ASSERT_FALSE(presence.is_discarded())
        << "presence JSON corrupted by a hostile username: " << body;

    const auto [stats_status, stats_body] = fixture_.http_get("/media_stats");
    ASSERT_EQ(stats_status, 200u);
    ASSERT_FALSE(
        nlohmann::json::parse(stats_body, nullptr, false).is_discarded());
}

TEST_F(WsServerHostileTest, AbortMidSignalingLeavesTheRoomConsistent)
{
    RawWsClient alice(fixture_.port(), ws_target(1, "alice"));
    ASSERT_FALSE(alice.read_until("welcome").empty());

    {
        RawWsClient bob(fixture_.port(), ws_target(1, "bob"));
        ASSERT_FALSE(bob.read_until("welcome").empty());
        ASSERT_FALSE(alice.read_until("peer_joined").empty());
        bob.send(R"({"type":"offer","sdp":"v=0)");
        bob.abort();
    }

    EXPECT_FALSE(alice.read_until("peer_left").empty())
        << "an aborted socket never produced peer_left";
    EXPECT_EQ(fixture_.active_sessions(), 1u);
}

TEST_F(WsServerHostileTest, HostileSdpIsRejectedAndTheRoomKeepsWorking)
{
    RawWsClient alice(fixture_.port(), ws_target(1, "alice"));
    ASSERT_FALSE(alice.read_until("welcome").empty());

    const std::string huge_mid = "a=mid:" + std::string(64 * 1024, 'a');
    const std::vector<std::string> hostile_sdp {
        "",
        "v=0",
        "not sdp at all\r\nrandom bytes",
        "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n",
        "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=mid:0\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=mid:0\r\n",
        "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 200\r\na=mid:0\r\n",
        "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=mid:0\r\n"
        "a=fingerprint:sha-256 not:a:real:fingerprint\r\n",
        "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
            + huge_mid + "\r\n",
    };
    for (const auto& sdp : hostile_sdp) {
        alice.send(offer_with_sdp(sdp, "voice"));
        alice.send(offer_with_sdp(sdp, "screen"));
    }
    alice.send(
        R"({"type":"candidate","candidate":"garbage","sdpMid":"0",)"
        R"("connection":"voice"})");

    RawWsClient bob(fixture_.port(), ws_target(1, "bob"));
    ASSERT_FALSE(bob.read_until("welcome").empty());
    EXPECT_FALSE(alice.read_until("peer_joined").empty());
    EXPECT_EQ(fixture_.active_sessions(), 2u);
}

TEST_F(WsServerHostileTest, HttpSurfaceRejectsWrongMethodsAndPaths)
{
    EXPECT_EQ(fixture_.http_get("/nope").first, 404u);
    EXPECT_EQ(fixture_.http_get("/channels/1").first, 404u);
    EXPECT_EQ(fixture_.http_get("/../etc/passwd").first, 404u);

    boost::asio::io_context io;
    tcp::socket socket(io);
    socket.connect(tcp::endpoint(
        boost::asio::ip::make_address("127.0.0.1"), fixture_.port()));
    http::request<http::string_body> request { http::verb::post, "/health",
        11 };
    request.set(http::field::host, "127.0.0.1");
    request.prepare_payload();
    http::write(socket, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    boost::system::error_code ec;
    http::read(socket, buffer, response, ec);
    ASSERT_FALSE(ec) << ec.message();
    EXPECT_GE(response.result_int(), 400u);
}

TEST(WsServerLifecycle, StartStopStormWithLiveSessionsDoesNotCrash)
{
    driscord::set_min_log_level(driscord::LogLevel::None);
    for (int round = 0; round < 5; ++round) {
        auto fixture = std::make_unique<SignalingServerFixture>();
        std::vector<std::unique_ptr<RawWsClient>> clients;
        for (int i = 0; i < 4; ++i) {
            clients.push_back(std::make_unique<RawWsClient>(
                fixture->port(), ws_target(1, "peer" + std::to_string(i))));
        }
        clients.front()->send(R"({"type":"offer","sdp":"v=0)");
        fixture.reset();
        for (auto& client : clients) {
            client->abort();
        }
    }
    SUCCEED();
}

TEST(WsServerLifecycle, RegistrationAfterStopIsRefused)
{
    boost::asio::io_context io;
    auto server = std::make_shared<driscord::WebSocketServer>(io, 0);
    server->stop();

    const auto welcome = server->register_and_build_welcome(
        driscord::PeerId { "late-peer" }, driscord::RoomId { "1" }, nullptr);

    EXPECT_FALSE(welcome.has_value());
    EXPECT_EQ(server->active_sessions(), 0u);
}

}
