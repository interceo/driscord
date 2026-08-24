#include "api/AuthManager.h"
#include "support/http_fixture.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using test_support::HttpFixture;
using test_support::HttpResponse;

namespace {

HttpResponse tokenResponse(const char* access = "acc-1",
    const char* refresh = "ref-1")
{
    const QJsonObject body {
        { "access_token", access },
        { "refresh_token", refresh },
        { "user_id", 42 },
        { "avatar_url", " /avatars/42.png " },
        { "display_name", "Neo" },
    };
    return { .body = QJsonDocument(body).toJson(QJsonDocument::Compact) };
}

} // namespace

// AuthManager owns the login/refresh/logout state machine that both QML and
// the media session key off (the signaling server authorizes with the same
// access token). The wire side is a real localhost HTTP server; the tests pin
// the persisted-session contract and the request-generation guard that keeps
// a late reply from resurrecting a logged-out session.
class TestAuthManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Redirect QSettings-backed SessionStore away from real config.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("driscord-test");
        QCoreApplication::setApplicationName("auth-manager-test");
    }

    void init() { SessionStore().clear(); }

    void loginSuccessAppliesTokensAndPersistsSession()
    {
        HttpFixture http;
        http.enqueue(tokenResponse());

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        AuthManager auth(&api, &store);
        QSignalSpy authChanged(&auth, &AuthManager::authChanged);
        QSignalSpy pendingChanged(&auth, &AuthManager::authPendingChanged);

        auth.login("alice", "hunter2");
        QVERIFY(auth.authPending());
        QTRY_VERIFY(auth.loggedIn());

        QCOMPARE(auth.username(), QStringLiteral("alice"));
        QCOMPARE(auth.userId(), 42);
        QCOMPARE(auth.displayName(), QStringLiteral("Neo"));
        QCOMPARE(auth.avatarUrl(), QStringLiteral("/avatars/42.png"));
        QCOMPARE(auth.accessToken(), QStringLiteral("acc-1"));
        QCOMPARE(api.accessToken(), QStringLiteral("acc-1"));
        QVERIFY(!auth.authPending());
        QCOMPARE(authChanged.count(), 1);
        QCOMPARE(pendingChanged.count(), 2);

        const auto persisted = store.load();
        QVERIFY(persisted.has_value());
        QCOMPARE(persisted->refreshToken, QStringLiteral("ref-1"));
        QCOMPARE(persisted->username, QStringLiteral("alice"));
        QCOMPARE(persisted->userId, 42);

        const auto& request = http.requests().first();
        QCOMPARE(request.path, QByteArray("/auth/login"));
        const auto body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value("username").toString(), QStringLiteral("alice"));
        QCOMPARE(body.value("password").toString(), QStringLiteral("hunter2"));
    }

    void loginFailureSurfacesServerDetail()
    {
        HttpFixture http;
        http.enqueue({ .status = 401, .body = R"({"detail":"wrong password"})" });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        AuthManager auth(&api, &store);
        QSignalSpy loginError(&auth, &AuthManager::loginError);

        auth.login("alice", "nope");
        QTRY_COMPARE(loginError.count(), 1);
        QCOMPARE(loginError.first().first().toString(),
            QStringLiteral("wrong password"));
        QVERIFY(!auth.loggedIn());
        QVERIFY(!auth.authPending());
        QVERIFY(!store.load().has_value());
    }

    void loginFailureWithoutDetailUsesFallbackMessage()
    {
        HttpFixture http;
        http.enqueue({ .status = 500, .body = "gateway exploded", .contentType = "text/plain" });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        AuthManager auth(&api, &store);
        QSignalSpy loginError(&auth, &AuthManager::loginError);

        auth.login("alice", "pw");
        QTRY_COMPARE(loginError.count(), 1);
        QCOMPARE(loginError.first().first().toString(),
            QStringLiteral("Login failed"));
    }

    void loginWhilePendingIsIgnored()
    {
        HttpFixture http;
        HttpResponse slow = tokenResponse();
        slow.delayMs = 100;
        http.enqueue(slow);

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        AuthManager auth(&api, &store);

        auth.login("alice", "pw");
        auth.login("mallory", "pw2");
        QTRY_VERIFY(auth.loggedIn());

        // Only the first attempt reached the wire; the state is alice's.
        QCOMPARE(http.requests().size(), 1);
        QCOMPARE(auth.username(), QStringLiteral("alice"));
    }

    void registerUserSuccessLogsIn()
    {
        HttpFixture http;
        http.enqueue(tokenResponse());

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        AuthManager auth(&api, &store);

        auth.registerUser("bob", "bob@example.com", "pw");
        QTRY_VERIFY(auth.loggedIn());
        QCOMPARE(auth.username(), QStringLiteral("bob"));

        const auto& request = http.requests().first();
        QCOMPARE(request.path, QByteArray("/auth/register"));
        const auto body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value("email").toString(),
            QStringLiteral("bob@example.com"));
    }

    void registerUserFailureSurfacesDetail()
    {
        HttpFixture http;
        http.enqueue({ .status = 409, .body = R"({"detail":"username taken"})" });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        AuthManager auth(&api, &store);
        QSignalSpy loginError(&auth, &AuthManager::loginError);

        auth.registerUser("bob", "bob@example.com", "pw");
        QTRY_COMPARE(loginError.count(), 1);
        QCOMPARE(loginError.first().first().toString(),
            QStringLiteral("username taken"));
        QVERIFY(!auth.loggedIn());
    }

    void logoutClearsStateTokenAndPersistedSession()
    {
        HttpFixture http;
        http.enqueue(tokenResponse());

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        AuthManager auth(&api, &store);
        auth.login("alice", "pw");
        QTRY_VERIFY(auth.loggedIn());

        QSignalSpy authChanged(&auth, &AuthManager::authChanged);
        auth.logout();

        QVERIFY(!auth.loggedIn());
        QCOMPARE(authChanged.count(), 1);
        QVERIFY(api.accessToken().isEmpty());
        QVERIFY(auth.username().isEmpty());
        QCOMPARE(auth.userId(), 0);
        QVERIFY(!store.load().has_value());
    }

    void restoreWithoutStoredSessionFailsWithoutTouchingTheNetwork()
    {
        HttpFixture http;
        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        AuthManager auth(&api, &store);
        QSignalSpy failed(&auth, &AuthManager::sessionRestoreFailed);

        auth.tryRestoreSession();
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(http.requests().isEmpty());
        QVERIFY(!auth.loggedIn());
    }

    void restoreSuccessRefreshesWithStoredToken()
    {
        HttpFixture http;
        http.enqueue(tokenResponse("acc-2", "ref-2"));

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        store.save({ "ref-old", "alice", 42 });
        AuthManager auth(&api, &store);
        QSignalSpy restored(&auth, &AuthManager::sessionRestored);

        auth.tryRestoreSession();
        QTRY_COMPARE(restored.count(), 1);
        QVERIFY(auth.loggedIn());
        QCOMPARE(auth.accessToken(), QStringLiteral("acc-2"));

        const auto& request = http.requests().first();
        QCOMPARE(request.path, QByteArray("/auth/refresh"));
        const auto body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value("refresh_token").toString(),
            QStringLiteral("ref-old"));

        // The rotated refresh token replaced the stored one.
        QCOMPARE(store.load()->refreshToken, QStringLiteral("ref-2"));
    }

    void restoreFailureClearsTheStoredSession()
    {
        HttpFixture http;
        http.enqueue({ .status = 401, .body = R"({"detail":"expired"})" });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        store.save({ "ref-dead", "alice", 42 });
        AuthManager auth(&api, &store);
        QSignalSpy failed(&auth, &AuthManager::sessionRestoreFailed);

        auth.tryRestoreSession();
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!auth.loggedIn());
        QVERIFY(auth.username().isEmpty());
        QCOMPARE(auth.userId(), 0);
        QVERIFY(!store.load().has_value());
    }

    void lateReplyAfterLogoutCannotResurrectTheSession()
    {
        HttpFixture http;
        HttpResponse slow = tokenResponse();
        slow.delayMs = 100;
        http.enqueue(slow);

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        SessionStore store;
        AuthManager auth(&api, &store);

        auth.login("alice", "pw");
        // Logout bumps the request generation before the reply lands.
        auth.logout();
        QVERIFY(!auth.authPending());

        // Give the delayed 200-with-tokens ample time to arrive and be dropped.
        QTest::qWait(400);
        QVERIFY(!auth.loggedIn());
        QVERIFY(api.accessToken().isEmpty());
        QVERIFY(!store.load().has_value());
    }
};

QTEST_GUILESS_MAIN(TestAuthManager)
#include "test_auth_manager.moc"
