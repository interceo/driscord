#include "ThumbnailProvider.h"
#include <QMutexLocker>

ThumbnailProvider::ThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void ThumbnailProvider::store(const QString& key, QImage image)
{
    QMutexLocker lock(&m_mutex);
    m_thumbs.insert(key, std::move(image));
}

QImage ThumbnailProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    QMutexLocker lock(&m_mutex);
    auto img = m_thumbs.value(id);
    if (size)
        *size = img.size();
    return img;
}
