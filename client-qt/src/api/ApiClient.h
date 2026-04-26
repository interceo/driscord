#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <functional>

class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject* parent = nullptr);

    void setBaseUrl(const QString& url);
    void setAccessToken(const QString& token);
    void clearAccessToken();

    using JsonCb = std::function<void(QNetworkReply::NetworkError, QJsonObject)>;
    using ArrayCb = std::function<void(QNetworkReply::NetworkError, QJsonArray)>;
    using BinaryCb = std::function<void(QNetworkReply::NetworkError, QByteArray)>;

    void get(const QString& path, JsonCb cb);
    void getArray(const QString& path, ArrayCb cb);
    void post(const QString& path, const QJsonObject& body, JsonCb cb);
    void patch(const QString& path, const QJsonObject& body, JsonCb cb);
    void del(const QString& path, JsonCb cb);
    void putMultipart(const QString& path,
        const QString& fieldName,
        const QString& fileName,
        const QByteArray& data,
        const QString& mimeType,
        JsonCb cb);

private:
    QNetworkRequest makeRequest(const QString& path) const;
    void handleReply(QNetworkReply* reply, JsonCb cb);

    QNetworkAccessManager m_nam;
    QString m_baseUrl;
    QString m_accessToken;
};
