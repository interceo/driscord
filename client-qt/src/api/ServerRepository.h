#pragma once
#include <QObject>
#include <QJsonArray>
#include "ApiClient.h"

class ServerRepository : public QObject {
    Q_OBJECT
public:
    explicit ServerRepository(ApiClient* api, QObject* parent = nullptr);

    void listServers(std::function<void(bool, QJsonArray)> cb);
    void createServer(const QString& name, const QString& description,
                      std::function<void(bool, QJsonObject)> cb);
    void listChannels(int serverId, std::function<void(bool, QJsonArray)> cb);
    void createChannel(int serverId, const QString& name, const QString& kind,
                       std::function<void(bool, QJsonObject)> cb);
    void createInvite(int serverId, std::function<void(bool, QJsonObject)> cb);
    void acceptInvite(const QString& code, std::function<void(bool, QJsonObject)> cb);

private:
    ApiClient* m_api;
};
