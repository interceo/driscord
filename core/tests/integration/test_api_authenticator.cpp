// ApiAuthenticator failure modes.
//
// test_signaling_auth covers the happy paths through the whole WebSocket
// handshake; this file points the authenticator at an API that is down, slow,
// broken or lying, and asserts that every such case fails closed — the
// callback fires exactly once with std::nullopt and nothing hangs.

#include "api_authenticator.hpp"
#include "fake_api_server.hpp"
#include "tcp_fault_proxy.hpp"
#include "utils/log.hpp"
#include "wait_helpers.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

using test_util::FakeApiServer;
using test_util::TcpFaultProxy;
using test_util::Waiter;

namespace {

struct SuppressLogs {
    SuppressLogs() { driscord::set_min_log_level(driscord::LogLevel::None); }
};
const SuppressLogs suppress_logs_on_startup;

// The authenticator's own request timeout is 5 s; every negative wait below
// must sit above it so a timing-out call still counts as "answered".
constexpr auto kBeyondRequestTimeout = std::chrono::seconds(8);

class ApiAuthenticatorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        work_.emplace(boost::asio::make_work_guard(io_));
        thread_ = std::thread([this] { io_.run(); });
    }

    void TearDown() override
    {
        work_.reset();
        io_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::shared_ptr<driscord::ApiAuthenticator> make_authenticator(
        const std::string& base_url)
    {
        auto authenticator
            = driscord::ApiAuthenticator::create(io_, base_url);
        EXPECT_NE(authenticator, nullptr);
        return authenticator;
    }

    // Runs one authorization and returns what the callback delivered, or
    // nullopt-of-nullopt if the callback never fired within the deadline.
    std::optional<std::optional<driscord::ApiAuthenticator::Identity>>
    authorize(driscord::ApiAuthenticator& authenticator,
        std::chrono::milliseconds deadline
        = std::chrono::duration_cast<std::chrono::milliseconds>(
            test_util::kDefaultTimeout))
    {
        auto waiter = std::make_shared<Waiter>();
        auto result = std::make_shared<
            std::optional<std::optional<driscord::ApiAuthenticator::Identity>>>();
        authenticator.authorize_channel("token-1", "42",
            [waiter, result](
                std::optional<driscord::ApiAuthenticator::Identity> identity) {
                *result = std::move(identity);
                waiter->signal();
            });
        if (!waiter->wait_for(deadline)) {
            return std::nullopt;
        }
        return *result;
    }

    boost::asio::io_context io_;
    std::optional<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>
        work_;
    std::thread thread_;
};

TEST_F(ApiAuthenticatorTest, RefusesWhenTheApiIsDown)
{
    FakeApiServer api;
    api.refuse_new_connections();
    auto authenticator = make_authenticator(api.base_url());

    const auto result = *authorize(*authenticator);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiAuthenticatorTest, RefusesWhenTheApiResetsTheConnection)
{
    FakeApiServer api;
    api.set_behavior(FakeApiServer::Behavior::ResetAfterAccept);
    auto authenticator = make_authenticator(api.base_url());

    const auto result = *authorize(*authenticator);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiAuthenticatorTest, RefusesWhenTheApiNeverAnswers)
{
    FakeApiServer api;
    api.set_behavior(FakeApiServer::Behavior::Stall);
    auto authenticator = make_authenticator(api.base_url());

    const auto answered = authorize(*authenticator,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            kBeyondRequestTimeout));
    ASSERT_TRUE(answered.has_value())
        << "the request timeout never fired the callback";
    EXPECT_FALSE(answered->has_value());
}

TEST_F(ApiAuthenticatorTest, RefusesOnHttp500)
{
    FakeApiServer api;
    api.set_response(500, R"({"detail": "boom"})");
    auto authenticator = make_authenticator(api.base_url());

    const auto result = *authorize(*authenticator);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiAuthenticatorTest, RefusesOnNonJsonBody)
{
    FakeApiServer api;
    api.set_response(200, "<html>definitely not json</html>");
    auto authenticator = make_authenticator(api.base_url());

    const auto result = *authorize(*authenticator);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiAuthenticatorTest, RefusesOnTruncatedBody)
{
    FakeApiServer api;
    api.set_behavior(FakeApiServer::Behavior::TruncateBody);
    auto authenticator = make_authenticator(api.base_url());

    const auto result = *authorize(*authenticator);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiAuthenticatorTest, RefusesAnIdentityWithoutUsername)
{
    FakeApiServer api;
    api.set_response(200, R"({"user_id": 7})");
    auto authenticator = make_authenticator(api.base_url());

    const auto result = *authorize(*authenticator);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiAuthenticatorTest, EmptyTokenIsRefusedWithoutContactingTheApi)
{
    FakeApiServer api;
    auto authenticator = make_authenticator(api.base_url());

    Waiter waiter;
    std::optional<driscord::ApiAuthenticator::Identity> delivered;
    authenticator->authorize_channel("", "42",
        [&](std::optional<driscord::ApiAuthenticator::Identity> identity) {
            delivered = std::move(identity);
            waiter.signal();
        });
    ASSERT_TRUE(waiter.wait_for());
    EXPECT_FALSE(delivered.has_value());
    EXPECT_TRUE(api.requests().empty());
}

TEST_F(ApiAuthenticatorTest, ConcurrentAuthorizationsAllComplete)
{
    FakeApiServer api;
    auto authenticator = make_authenticator(api.base_url());

    constexpr int kCalls = 16;
    test_util::EventCollector<bool> outcomes;
    for (int i = 0; i < kCalls; ++i) {
        authenticator->authorize_channel("token", std::to_string(i),
            [&outcomes](
                std::optional<driscord::ApiAuthenticator::Identity> identity) {
                outcomes.push(identity.has_value());
            });
    }
    ASSERT_TRUE(outcomes.wait_for_count(kCalls));
    for (const bool authorized : outcomes.snapshot()) {
        EXPECT_TRUE(authorized);
    }
    EXPECT_EQ(api.requests().size(), kCalls);
}

TEST_F(ApiAuthenticatorTest, RefusesWhenAConnectingProxyRefuses)
{
    FakeApiServer api;
    TcpFaultProxy proxy("127.0.0.1", api.port(),
        TcpFaultProxy::Config { .refuse = true });
    auto authenticator
        = make_authenticator("http://127.0.0.1:" + std::to_string(proxy.port()));

    const auto result = *authorize(*authenticator);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ApiAuthenticatorTest, RefusesWhenTheApiHopIsTooSlow)
{
    FakeApiServer api;
    // A latency well past the authenticator's 5 s request timeout: the call
    // must time out and fail closed rather than block a handshake forever.
    TcpFaultProxy proxy("127.0.0.1", api.port(),
        TcpFaultProxy::Config { .latency = std::chrono::seconds(9) });
    auto authenticator
        = make_authenticator("http://127.0.0.1:" + std::to_string(proxy.port()));

    const auto answered = authorize(*authenticator,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            kBeyondRequestTimeout));
    ASSERT_TRUE(answered.has_value());
    EXPECT_FALSE(answered->has_value());
}

} // namespace