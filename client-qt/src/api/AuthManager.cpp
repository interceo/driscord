#include "AuthManager.h"
#include <QJsonObject>
#include <QNetworkReply>

AuthManager::AuthManager(ApiClient* api, SessionStore* session, QObject* parent)
    : QObject(parent), m_api(api), m_session(session) {}

bool    AuthManager::loggedIn()    const { return m_loggedIn; }
QString AuthManager::username()    const { return m_username; }
int     AuthManager::userId()      const { return m_userId; }
QString AuthManager::avatarUrl()   const { return m_avatarUrl; }
QString AuthManager::displayName() const { return m_displayName.isEmpty() ? m_username : m_displayName; }

void AuthManager::applyTokenResponse(const QJsonObject& json, const QString& username) {
    auto access  = json["access_token"].toString();
    auto refresh = json["refresh_token"].toString();
    auto uid     = json["user_id"].toInt();
    m_api->setAccessToken(access);
    m_refreshToken = refresh;
    m_username     = username;
    m_userId       = uid;
    m_avatarUrl    = json["avatar_url"].toString();
    m_displayName  = json["display_name"].toString();
    m_loggedIn     = true;
    m_session->save({refresh, username, uid});
    emit authChanged();
}

void AuthManager::login(const QString& username, const QString& password) {
    QJsonObject body{{"username", username}, {"password", password}};
    m_api->post("/auth/login", body, [this, username](QNetworkReply::NetworkError err, QJsonObject json) {
        if (err != QNetworkReply::NoError) {
            emit loginError(json.value("detail").toString("Login failed"));
            return;
        }
        applyTokenResponse(json, username);
    });
}

void AuthManager::registerUser(const QString& username, const QString& email, const QString& password) {
    QJsonObject body{{"username", username}, {"email", email}, {"password", password}};
    m_api->post("/auth/register", body, [this, username](QNetworkReply::NetworkError err, QJsonObject json) {
        if (err != QNetworkReply::NoError) {
            emit loginError(json.value("detail").toString("Registration failed"));
            return;
        }
        applyTokenResponse(json, username);
    });
}

void AuthManager::logout() {
    m_api->clearAccessToken();
    m_session->clear();
    m_refreshToken.clear();
    m_username.clear();
    m_userId    = 0;
    m_loggedIn  = false;
    emit authChanged();
}

void AuthManager::tryRestoreSession() {
    auto data = m_session->load();
    if (!data) { emit sessionRestoreFailed(); return; }

    m_username = data->username;
    m_userId   = data->userId;

    QJsonObject body{{"refresh_token", data->refreshToken}};
    m_api->post("/auth/refresh", body, [this, username = data->username](QNetworkReply::NetworkError err, QJsonObject json) {
        if (err != QNetworkReply::NoError) {
            m_session->clear();
            emit sessionRestoreFailed();
            return;
        }
        applyTokenResponse(json, username);
        emit sessionRestored();
    });
}
