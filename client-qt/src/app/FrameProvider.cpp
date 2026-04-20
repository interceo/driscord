#include "FrameProvider.h"
#include <QMutexLocker>

FrameProvider::FrameProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

void FrameProvider::updateFrame(const QString& peerId, const uchar* rgba, int w, int h) {
    QMutexLocker lock(&m_mutex);
    m_frames[peerId] = QImage(rgba, w, h, QImage::Format_RGBA8888).copy();
}

void FrameProvider::removeFrame(const QString& peerId) {
    QMutexLocker lock(&m_mutex);
    m_frames.remove(peerId);
}

QImage FrameProvider::requestImage(const QString& id, QSize* size, const QSize&) {
    QMutexLocker lock(&m_mutex);
    auto img = m_frames.value(id);
    if (size) *size = img.size();
    return img;
}
