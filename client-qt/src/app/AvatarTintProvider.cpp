#include "AvatarTintProvider.h"

#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

AvatarTintProvider::AvatarTintProvider(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QColor AvatarTintProvider::colorFor(const QString& url) const
{
    return m_cache.value(url, QColor());
}

void AvatarTintProvider::prefetch(const QString& url)
{
    if (url.isEmpty())
        return;
    if (m_cache.contains(url)) {
        emit colorReady(url, m_cache.value(url));
        return;
    }
    if (m_inflight.contains(url))
        return;
    m_inflight.insert(url);

    QNetworkRequest req((QUrl(url)));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
        QNetworkRequest::PreferCache);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        m_inflight.remove(url);
        if (reply->error() == QNetworkReply::NoError) {
            QImage img;
            if (img.loadFromData(reply->readAll())) {
                // Downscale to 1×1 with smooth (bilinear) transformation —
                // produces a single pixel that approximates the avatar's
                // average color.
                QImage one = img.scaled(1, 1, Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
                QColor color(one.pixel(0, 0));
                m_cache.insert(url, color);
                emit colorReady(url, color);
            }
        }
        reply->deleteLater();
    });
}
