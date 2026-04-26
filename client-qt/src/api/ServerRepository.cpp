#include "ServerRepository.h"
#include <QJsonObject>
#include <QNetworkReply>

ServerRepository::ServerRepository(ApiClient* api, QObject* parent)
    : QObject(parent)
    , m_api(api)
{
}

void ServerRepository::listServers(std::function<void(bool, QJsonArray)> cb)
{
    m_api->getArray("/servers/", [cb](QNetworkReply::NetworkError err, QJsonArray arr) {
        cb(err == QNetworkReply::NoError, arr);
    });
}

void ServerRepository::createServer(const QString& name, const QString& description,
    std::function<void(bool, QJsonObject)> cb)
{
    QJsonObject body { { "name", name }, { "description", description } };
    m_api->post("/servers/", body, [cb](QNetworkReply::NetworkError err, QJsonObject json) {
        cb(err == QNetworkReply::NoError, json);
    });
}

void ServerRepository::listChannels(int serverId, std::function<void(bool, QJsonArray)> cb)
{
    m_api->getArray(QStringLiteral("/servers/%1/channels").arg(serverId),
        [cb](QNetworkReply::NetworkError err, QJsonArray arr) {
            cb(err == QNetworkReply::NoError, arr);
        });
}

void ServerRepository::createChannel(int serverId, const QString& name, const QString& kind,
    std::function<void(bool, QJsonObject)> cb)
{
    QJsonObject body { { "name", name }, { "kind", kind } };
    m_api->post(QStringLiteral("/servers/%1/channels").arg(serverId), body,
        [cb](QNetworkReply::NetworkError err, QJsonObject json) {
            cb(err == QNetworkReply::NoError, json);
        });
}

void ServerRepository::createInvite(int serverId, std::function<void(bool, QJsonObject)> cb)
{
    m_api->post(QStringLiteral("/servers/%1/invites").arg(serverId), { },
        [cb](QNetworkReply::NetworkError err, QJsonObject json) {
            cb(err == QNetworkReply::NoError, json);
        });
}

void ServerRepository::acceptInvite(const QString& code, std::function<void(bool, QJsonObject)> cb)
{
    m_api->post(QStringLiteral("/invites/%1/accept").arg(code), { },
        [cb](QNetworkReply::NetworkError err, QJsonObject json) {
            cb(err == QNetworkReply::NoError, json);
        });
}
