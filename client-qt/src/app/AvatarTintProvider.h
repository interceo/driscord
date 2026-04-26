#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QColor>
#include <QString>

class QNetworkAccessManager;

// Computes and caches a single representative color for each avatar URL by
// fetching the image once and downscaling it to 1×1 with smooth filtering.
// QML reads `colorFor(url)` synchronously (returns invalid QColor until ready)
// and listens for `colorReady(url, color)` to refresh its bindings.
class AvatarTintProvider : public QObject {
    Q_OBJECT
public:
    explicit AvatarTintProvider(QObject* parent = nullptr);

    Q_INVOKABLE QColor colorFor(const QString& url) const;
    Q_INVOKABLE void   prefetch(const QString& url);

signals:
    void colorReady(const QString& url, const QColor& color);

private:
    QNetworkAccessManager*    m_nam;
    QHash<QString, QColor>    m_cache;
    QSet<QString>             m_inflight;
};
