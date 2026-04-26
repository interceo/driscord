#include "UserRepository.h"
#include <QNetworkReply>
#include <QUrl>

UserRepository::UserRepository(ApiClient* api, QObject* parent)
    : QObject(parent)
    , m_api(api)
{
}

void UserRepository::getUserById(int id, std::function<void(bool, QJsonObject)> cb)
{
    m_api->get(QStringLiteral("/users/%1").arg(id),
        [cb](QNetworkReply::NetworkError err, QJsonObject json) {
            cb(err == QNetworkReply::NoError, json);
        });
}

void UserRepository::getMe(std::function<void(bool, QJsonObject)> cb)
{
    m_api->get(QStringLiteral("/users/me"),
        [cb](QNetworkReply::NetworkError err, QJsonObject json) {
            cb(err == QNetworkReply::NoError, json);
        });
}

void UserRepository::updateProfile(int userId, const QString& displayName,
    std::function<void(bool, QJsonObject)> cb)
{
    QJsonObject body { { "display_name", displayName } };
    m_api->patch(QStringLiteral("/users/%1").arg(userId), body,
        [cb](QNetworkReply::NetworkError err, QJsonObject json) {
            cb(err == QNetworkReply::NoError, json);
        });
}

void UserRepository::uploadAvatar(int userId, const QByteArray& data, const QString& ext,
    std::function<void(bool, QJsonObject)> cb)
{
    QString mime = (ext == "png") ? "image/png" : "image/jpeg";
    m_api->putMultipart(
        QStringLiteral("/users/%1/avatar").arg(userId),
        "file", QStringLiteral("avatar.%1").arg(ext),
        data, mime,
        [cb](QNetworkReply::NetworkError err, QJsonObject json) {
            cb(err == QNetworkReply::NoError, json);
        });
}

void UserRepository::getUserByUsername(const QString& username,
    std::function<void(bool, QJsonObject)> cb)
{
    auto encoded = QUrl::toPercentEncoding(username);
    m_api->get(QStringLiteral("/users/lookup?username=%1").arg(QString(encoded)),
        [cb](QNetworkReply::NetworkError err, QJsonObject json) {
            cb(err == QNetworkReply::NoError, json);
        });
}
