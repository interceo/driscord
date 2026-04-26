#pragma once
#include "ApiClient.h"
#include "SessionStore.h"
#include <QObject>
#include <QString>

class AuthManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY authChanged)
    Q_PROPERTY(QString username READ username NOTIFY authChanged)
    Q_PROPERTY(int userId READ userId NOTIFY authChanged)
    Q_PROPERTY(QString avatarUrl READ avatarUrl NOTIFY authChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY authChanged)
public:
    explicit AuthManager(ApiClient* api, SessionStore* session, QObject* parent = nullptr);

    bool loggedIn() const;
    QString username() const;
    int userId() const;
    QString avatarUrl() const;
    QString displayName() const;

    Q_INVOKABLE void login(const QString& username, const QString& password);
    Q_INVOKABLE void registerUser(const QString& username, const QString& email, const QString& password);
    Q_INVOKABLE void logout();

    Q_INVOKABLE void tryRestoreSession();

signals:
    void authChanged();
    void loginError(const QString& message);
    void sessionRestored();
    void sessionRestoreFailed();

private:
    void applyTokenResponse(const QJsonObject& json, const QString& username);

    ApiClient* m_api;
    SessionStore* m_session;
    QString m_username;
    QString m_refreshToken;
    QString m_avatarUrl;
    QString m_displayName;
    int m_userId = 0;
    bool m_loggedIn = false;
};
