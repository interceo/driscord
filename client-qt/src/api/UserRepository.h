#pragma once
#include <QObject>
#include <QJsonObject>
#include <functional>
#include "ApiClient.h"

class UserRepository : public QObject {
    Q_OBJECT
public:
    explicit UserRepository(ApiClient* api, QObject* parent = nullptr);

    void getUserById(int id, std::function<void(bool, QJsonObject)> cb);
    void updateProfile(int userId, const QString& displayName,
                       std::function<void(bool, QJsonObject)> cb);
    void uploadAvatar(int userId, const QByteArray& data, const QString& ext,
                      std::function<void(bool, QJsonObject)> cb);
    void getUserByUsername(const QString& username, std::function<void(bool, QJsonObject)> cb);

private:
    ApiClient* m_api;
};
