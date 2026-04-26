#pragma once
#include <QQuickImageProvider>
#include <QHash>
#include <QImage>
#include <QMutex>

class ThumbnailProvider : public QQuickImageProvider {
public:
    ThumbnailProvider();

    void store(const QString& key, QImage image);
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    QMutex                 m_mutex;
    QHash<QString, QImage> m_thumbs;
};
