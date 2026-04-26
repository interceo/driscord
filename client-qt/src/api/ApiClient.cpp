#include "ApiClient.h"
#include <QDebug>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrl>

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
{
}

void ApiClient::setBaseUrl(const QString& url) { m_baseUrl = url; }
void ApiClient::setAccessToken(const QString& t) { m_accessToken = t; }
void ApiClient::clearAccessToken() { m_accessToken.clear(); }

QNetworkRequest ApiClient::makeRequest(const QString& path) const
{
    QNetworkRequest req(QUrl(m_baseUrl + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_accessToken.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_accessToken).toUtf8());
    return req;
}

void ApiClient::handleReply(QNetworkReply* reply, JsonCb cb)
{
    QString verb;
    switch (reply->operation()) {
    case QNetworkAccessManager::GetOperation:
        verb = "GET";
        break;
    case QNetworkAccessManager::PostOperation:
        verb = "POST";
        break;
    case QNetworkAccessManager::PutOperation:
        verb = "PUT";
        break;
    case QNetworkAccessManager::DeleteOperation:
        verb = "DELETE";
        break;
    case QNetworkAccessManager::CustomOperation:
        verb = reply->request().attribute(QNetworkRequest::CustomVerbAttribute).toString();
        break;
    default:
        verb = "?";
        break;
    }
    QString url = reply->url().toString();
    qDebug().noquote() << "[api]" << verb << url;

    connect(reply, &QNetworkReply::finished, this, [reply, cb, verb, url]() {
        auto err = reply->error();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonObject obj;
        if (err == QNetworkReply::NoError || reply->bytesAvailable()) {
            auto doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject())
                obj = doc.object();
        }
        if (err == QNetworkReply::NoError)
            qDebug().noquote() << "[api]" << verb << url << "→" << status;
        else
            qDebug().noquote() << "[api]" << verb << url << "→ ERR" << err << reply->errorString();
        cb(err, obj);
        reply->deleteLater();
    });
}

void ApiClient::get(const QString& path, JsonCb cb)
{
    handleReply(m_nam.get(makeRequest(path)), cb);
}

void ApiClient::getArray(const QString& path, ArrayCb cb)
{
    auto* reply = m_nam.get(makeRequest(path));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        auto err = reply->error();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonArray arr;
        if (err == QNetworkReply::NoError || reply->bytesAvailable()) {
            auto doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isArray())
                arr = doc.array();
        }
        if (err == QNetworkReply::NoError)
            qDebug().noquote() << "[api] GET" << reply->url().toString() << "→" << status;
        else
            qDebug().noquote() << "[api] GET" << reply->url().toString() << "→ ERR" << err << reply->errorString();
        cb(err, arr);
        reply->deleteLater();
    });
}

void ApiClient::post(const QString& path, const QJsonObject& body, JsonCb cb)
{
    auto req = makeRequest(path);
    handleReply(m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact)), cb);
}

void ApiClient::patch(const QString& path, const QJsonObject& body, JsonCb cb)
{
    auto req = makeRequest(path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    handleReply(m_nam.sendCustomRequest(req, "PATCH",
                    QJsonDocument(body).toJson(QJsonDocument::Compact)),
        cb);
}

void ApiClient::del(const QString& path, JsonCb cb)
{
    handleReply(m_nam.deleteResource(makeRequest(path)), cb);
}

void ApiClient::putMultipart(const QString& path,
    const QString& fieldName,
    const QString& fileName,
    const QByteArray& data,
    const QString& mimeType,
    JsonCb cb)
{
    auto* mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"").arg(fieldName, fileName));
    part.setBody(data);
    mp->append(part);

    auto req = makeRequest(path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, { });
    if (!m_accessToken.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_accessToken).toUtf8());

    auto* reply = m_nam.put(req, mp);
    mp->setParent(reply);
    handleReply(reply, cb);
}
