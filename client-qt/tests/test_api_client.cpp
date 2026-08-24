#include "api/ApiClient.h"
#include "support/http_fixture.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTest>

#include <optional>

using test_support::HttpFixture;
using test_support::HttpResponse;

// ApiClient is the single HTTP seam of the client: every repository and the
// auth flow go through it. These tests speak real HTTP over localhost and pin
// its wire contract — verb, path, headers, body — and its error-delivery
// contract: the callback always fires exactly once, and a JSON error body is
// still parsed so callers can surface the server's "detail".
class TestApiClient : public QObject {
    Q_OBJECT

private slots:
    void getParsesObjectAndSendsBearerToken()
    {
        HttpFixture http;
        QVERIFY(http.listening());
        http.enqueue({ .body = R"({"ok":true})" });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        api.setAccessToken("token-1");

        std::optional<QJsonObject> result;
        api.get("/users/me", [&](QNetworkReply::NetworkError err, QJsonObject obj) {
            QCOMPARE(err, QNetworkReply::NoError);
            result = obj;
        });
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->value("ok").toBool());

        QCOMPARE(http.requests().size(), 1);
        const auto& request = http.requests().first();
        QCOMPARE(request.method, QByteArray("GET"));
        QCOMPARE(request.path, QByteArray("/users/me"));
        QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer token-1"));
    }

    void getWithoutTokenOmitsAuthorizationHeader()
    {
        HttpFixture http;
        ApiClient api;
        api.setBaseUrl(http.baseUrl());

        std::optional<QJsonObject> result;
        api.get("/updates/check", [&](QNetworkReply::NetworkError, QJsonObject obj) {
            result = obj;
        });
        QTRY_VERIFY(result.has_value());
        QVERIFY(!http.requests().first().headers.contains("authorization"));
    }

    void clearedTokenIsNotSentAnymore()
    {
        HttpFixture http;
        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        api.setAccessToken("stale");
        api.clearAccessToken();

        std::optional<QJsonObject> result;
        api.get("/x", [&](QNetworkReply::NetworkError, QJsonObject obj) { result = obj; });
        QTRY_VERIFY(result.has_value());
        QVERIFY(!http.requests().first().headers.contains("authorization"));
    }

    void errorStatusStillDeliversParsedBody()
    {
        HttpFixture http;
        http.enqueue({ .status = 401, .body = R"({"detail":"bad credentials"})" });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());

        std::optional<QNetworkReply::NetworkError> error;
        QJsonObject body;
        api.post("/auth/login", { }, [&](QNetworkReply::NetworkError err, QJsonObject obj) {
            error = err;
            body = obj;
        });
        QTRY_VERIFY(error.has_value());
        QCOMPARE(*error, QNetworkReply::AuthenticationRequiredError);
        QCOMPARE(body.value("detail").toString(), QStringLiteral("bad credentials"));
    }

    void serverErrorDeliversErrorNotJson()
    {
        HttpFixture http;
        http.enqueue({ .status = 500, .body = "boom", .contentType = "text/plain" });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());

        std::optional<QNetworkReply::NetworkError> error;
        QJsonObject body;
        api.get("/x", [&](QNetworkReply::NetworkError err, QJsonObject obj) {
            error = err;
            body = obj;
        });
        QTRY_VERIFY(error.has_value());
        QCOMPARE(*error, QNetworkReply::InternalServerError);
        QVERIFY(body.isEmpty());
    }

    void nonJsonSuccessYieldsEmptyObject()
    {
        HttpFixture http;
        http.enqueue({ .body = "<html>not json</html>", .contentType = "text/html" });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());

        std::optional<QJsonObject> result;
        api.get("/x", [&](QNetworkReply::NetworkError err, QJsonObject obj) {
            QCOMPARE(err, QNetworkReply::NoError);
            result = obj;
        });
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->isEmpty());
    }

    void getArrayParsesArrayAndTurnsObjectIntoEmpty()
    {
        HttpFixture http;
        http.enqueue({ .body = R"([1,2,3])" });
        http.enqueue({ .body = R"({"not":"array"})" });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());

        std::optional<QJsonArray> first;
        api.getArray("/channels", [&](QNetworkReply::NetworkError err, QJsonArray arr) {
            QCOMPARE(err, QNetworkReply::NoError);
            first = arr;
        });
        QTRY_VERIFY(first.has_value());
        QCOMPARE(first->size(), 3);

        std::optional<QJsonArray> second;
        api.getArray("/channels", [&](QNetworkReply::NetworkError, QJsonArray arr) {
            second = arr;
        });
        QTRY_VERIFY(second.has_value());
        QVERIFY(second->isEmpty());
    }

    void postSendsCompactJsonBody()
    {
        HttpFixture http;
        ApiClient api;
        api.setBaseUrl(http.baseUrl());

        std::optional<QJsonObject> result;
        api.post("/servers", { { "name", "room" } },
            [&](QNetworkReply::NetworkError, QJsonObject obj) { result = obj; });
        QTRY_VERIFY(result.has_value());

        const auto& request = http.requests().first();
        QCOMPARE(request.method, QByteArray("POST"));
        QCOMPARE(request.headers.value("content-type"), QByteArray("application/json"));
        QCOMPARE(request.body, QByteArray(R"({"name":"room"})"));
    }

    void patchUsesCustomVerbWithBody()
    {
        HttpFixture http;
        ApiClient api;
        api.setBaseUrl(http.baseUrl());

        std::optional<QJsonObject> result;
        api.patch("/users/me", { { "display_name", "Neo" } },
            [&](QNetworkReply::NetworkError, QJsonObject obj) { result = obj; });
        QTRY_VERIFY(result.has_value());

        const auto& request = http.requests().first();
        QCOMPARE(request.method, QByteArray("PATCH"));
        QCOMPARE(request.body, QByteArray(R"({"display_name":"Neo"})"));
    }

    void delSendsDelete()
    {
        HttpFixture http;
        ApiClient api;
        api.setBaseUrl(http.baseUrl());

        std::optional<QJsonObject> result;
        api.del("/invites/7", [&](QNetworkReply::NetworkError, QJsonObject obj) {
            result = obj;
        });
        QTRY_VERIFY(result.has_value());
        QCOMPARE(http.requests().first().method, QByteArray("DELETE"));
    }

    void abruptCloseStillInvokesCallbackWithError()
    {
        HttpFixture http;
        // QNetworkAccessManager silently retries an idempotent request once
        // after an unexpected connection close, so the abort must be scripted
        // for the retry too or it would hit the default 200.
        http.enqueue({ .closeWithoutResponse = true });
        http.enqueue({ .closeWithoutResponse = true });

        ApiClient api;
        api.setBaseUrl(http.baseUrl());

        std::optional<QNetworkReply::NetworkError> error;
        api.get("/x", [&](QNetworkReply::NetworkError err, QJsonObject) { error = err; });
        QTRY_VERIFY(error.has_value());
        QVERIFY(*error != QNetworkReply::NoError);
    }

    void putMultipartSendsFormDataWithToken()
    {
        HttpFixture http;
        ApiClient api;
        api.setBaseUrl(http.baseUrl());
        api.setAccessToken("token-2");

        std::optional<QJsonObject> result;
        api.putMultipart("/users/me/avatar", "file", "avatar.png",
            QByteArray("\x89PNG-bytes"), "image/png",
            [&](QNetworkReply::NetworkError, QJsonObject obj) { result = obj; });
        QTRY_VERIFY(result.has_value());

        const auto& request = http.requests().first();
        QCOMPARE(request.method, QByteArray("PUT"));
        QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer token-2"));
        QVERIFY(request.headers.value("content-type")
                .startsWith("multipart/form-data"));
        QVERIFY(request.body.contains("name=\"file\""));
        QVERIFY(request.body.contains("filename=\"avatar.png\""));
        QVERIFY(request.body.contains("PNG-bytes"));
    }
};

QTEST_GUILESS_MAIN(TestApiClient)
#include "test_api_client.moc"
