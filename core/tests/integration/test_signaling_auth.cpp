#include "api_authenticator.hpp"
#include "fake_api_server.hpp"
#include "rtc_cleanup_env.hpp"
#include "signaling_test_fixture.hpp"
#include "utils/log.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

using test_util::FakeApiServer;
using test_util::SignalingServerFixture;

namespace {

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
const SuppressLogs suppress_logs_on_startup;

// A refused handshake never produces a welcome, so the negative cases wait for
// the absence of one. Deliberately short: the server answers 401 immediately
// once the API has replied.
constexpr auto kHandshakeTimeout = std::chrono::seconds(3);

// Opens a signaling WebSocket and reports whether the server accepted it.
// Acceptance means a welcome frame arrived: an upgrade that is answered with
// 401 shows up as an error/close instead.
class ProbeSocket {
public:
    bool connect(const std::string& url)
    {
        socket_.onMessage([this](auto message) {
            if (const auto* text = std::get_if<std::string>(&message)) {
                if (text->find("\"welcome\"") != std::string::npos) {
                    settle(true);
                }
            }
        });
        socket_.onError([this](std::string) { settle(false); });
        socket_.onClosed([this] { settle(false); });

        socket_.open(url);

        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, kHandshakeTimeout,
            [this] { return settled_; });
        return welcomed_;
    }

    ~ProbeSocket()
    {
        socket_.resetCallbacks();
        socket_.close();
    }

private:
    void settle(bool welcomed)
    {
        {
            std::scoped_lock lock(mutex_);
            if (settled_) {
                return;
            }
            settled_ = true;
            welcomed_ = welcomed;
        }
        changed_.notify_all();
    }

    rtc::WebSocket socket_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool settled_ = false;
    bool welcomed_ = false;
};

} // namespace

TEST(ApiAuthenticatorConfiguration, RejectsAmbiguousOrInvalidUrls)
{
    boost::asio::io_context io;
    EXPECT_EQ(driscord::ApiAuthenticator::create(io, "api.internal:8000"),
        nullptr);
    EXPECT_EQ(driscord::ApiAuthenticator::create(io, "https://api.internal"),
        nullptr);
    EXPECT_EQ(driscord::ApiAuthenticator::create(io, "http://api.internal:bad"),
        nullptr);
    EXPECT_EQ(driscord::ApiAuthenticator::create(io, "http://api.internal:70000"),
        nullptr);
    EXPECT_EQ(driscord::ApiAuthenticator::create(io, "http://user@api.internal"),
        nullptr);
    EXPECT_EQ(driscord::ApiAuthenticator::create(io, "http://api.internal/v1"),
        nullptr);
}

TEST(ApiAuthenticatorConfiguration, ParsesAPlainHttpAuthority)
{
    boost::asio::io_context io;
    const auto defaultPort
        = driscord::ApiAuthenticator::create(io, "http://api.internal/");
    ASSERT_NE(defaultPort, nullptr);
    EXPECT_EQ(defaultPort->host(), "api.internal");
    EXPECT_EQ(defaultPort->port(), "80");

    const auto customPort
        = driscord::ApiAuthenticator::create(io, "http://127.0.0.1:9002");
    ASSERT_NE(customPort, nullptr);
    EXPECT_EQ(customPort->host(), "127.0.0.1");
    EXPECT_EQ(customPort->port(), "9002");
}

class SignalingAuthTest : public ::testing::Test {
protected:
    SignalingAuthTest()
        : server(driscord::sfu::RtpFaultConfig { }, api.base_url())
    {
    }

    FakeApiServer api;
    SignalingServerFixture server;
};

TEST_F(SignalingAuthTest, AcceptsAChannelTheApiAuthorizes)
{
    api.set_response(200, R"({"user_id": 7, "username": "alice"})");

    ProbeSocket socket;
    ASSERT_TRUE(socket.connect(server.ws_url(42) + "?t=good-token"));

    const auto requests = api.requests();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests.front().target, "/channels/42/access");
    EXPECT_EQ(requests.front().authorization, "Bearer good-token");
}

TEST_F(SignalingAuthTest, RejectsAChannelTheApiRefuses)
{
    api.set_response(403, R"({"detail": "Not a server member"})");

    ProbeSocket socket;
    EXPECT_FALSE(socket.connect(server.ws_url(42) + "?t=other-users-token"));
    EXPECT_EQ(server.active_sessions(), 0u);
}

TEST_F(SignalingAuthTest, RejectsAMissingToken)
{
    api.set_response(200, R"({"user_id": 7, "username": "alice"})");

    ProbeSocket socket;
    EXPECT_FALSE(socket.connect(server.ws_url(42)));
    // No token means no question worth asking the API.
    EXPECT_TRUE(api.requests().empty());
    EXPECT_EQ(server.active_sessions(), 0u);
}

TEST_F(SignalingAuthTest, UsesTheIdentityFromTheApiNotTheQueryString)
{
    api.set_response(200, R"({"user_id": 7, "username": "alice"})");

    ProbeSocket socket;
    ASSERT_TRUE(socket.connect(
        server.ws_url(42) + "?u=impersonated&t=good-token"));

    // Presence is where the resolved username surfaces.
    const auto presence = server.http_get("/presence", "good-token");
    ASSERT_EQ(presence.first, 200u);
    EXPECT_NE(presence.second.find("\"alice\""), std::string::npos)
        << presence.second;
    EXPECT_EQ(presence.second.find("impersonated"), std::string::npos)
        << presence.second;
}

TEST_F(SignalingAuthTest, ReadOnlyEndpointsRequireAToken)
{
    api.set_response(401, R"({"detail": "Invalid token"})");
    EXPECT_EQ(server.http_get("/presence").first, 401u);
    EXPECT_EQ(server.http_get("/media_stats", "stale-token").first, 401u);

    api.set_response(200, R"({"id": 7, "username": "alice"})");
    const auto authorized = server.http_get("/presence", "good-token");
    EXPECT_EQ(authorized.first, 200u);
    EXPECT_EQ(api.requests().back().target, "/users/me");
}

// /media_stats is the only window into the SFU. Publishing counters per room
// is what tells an operator whether media reaches the server, the subscribers,
// or neither.
TEST_F(SignalingAuthTest, MediaStatsReportPerRoomRouterCounters)
{
    api.set_response(200, R"({"user_id": 7, "username": "alice"})");
    ProbeSocket socket;
    ASSERT_TRUE(socket.connect(server.ws_url(42) + "?t=good-token"));

    const auto stats = server.http_get("/media_stats", "good-token");
    ASSERT_EQ(stats.first, 200u);
    const auto body = nlohmann::json::parse(stats.second);
    ASSERT_TRUE(body.contains("42")) << stats.second;

    const auto& room = body["42"];
    EXPECT_EQ(room["sessions"], 1);
    EXPECT_EQ(room["streamingPeers"], 0);
    ASSERT_TRUE(room.contains("voice"));
    ASSERT_TRUE(room.contains("screen"));
    EXPECT_EQ(room["voice"]["packetsOut"], 0);
    EXPECT_EQ(room["screen"]["videoPacketsIn"], 0);
    EXPECT_EQ(room["screen"]["keyframeRequests"], 0);
}

// Probes have no credentials; if health checking needed one, enabling
// authorization would take the deployment down instead of securing it.
TEST_F(SignalingAuthTest, HealthEndpointStaysOpenAndEmpty)
{
    api.set_response(401, R"({"detail": "Invalid token"})");

    const auto health = server.http_get("/health");
    EXPECT_EQ(health.first, 200u);
    EXPECT_NE(health.second.find("\"ok\""), std::string::npos);
    EXPECT_TRUE(api.requests().empty());
}
