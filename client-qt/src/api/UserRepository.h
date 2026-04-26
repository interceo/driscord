#pragma once
#include "ApiClient.h"
#include <QJsonObject>
#include <QObject>
#include <functional>

class UserRepository : public QObject {
    Q_OBJECT
public:
    explicit UserRepository(ApiClient* api, QObject* parent = nullptr);

    void getUserById(int id, std::function<void(bool, QJsonObject)> cb);
    void getMe(std::function<void(bool, QJsonObject)> cb);
    void updateProfile(int userId, const QString& displayName,
        std::function<void(bool, QJsonObject)> cb);
    void uploadAvatar(int userId, const QByteArray& data, const QString& ext,
        std::function<void(bool, QJsonObject)> cb);
    void getUserByUsername(const QString& username, std::function<void(bool, QJsonObject)> cb);

private:
    ApiClient* m_api;
};
