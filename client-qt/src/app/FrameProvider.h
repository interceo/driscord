#pragma once
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// Thread-safe image provider for live video frames.
// DriscordBridge calls updateFrame() from any thread;
// QML Image{} requests frames via "image://frames/<peerId>".
class FrameProvider : public QQuickImageProvider {
public:
    FrameProvider();

    void updateFrame(const QString& peerId, const uchar* rgba, int w, int h);
    void removeFrame(const QString& peerId);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    QMutex m_mutex;
    QHash<QString, QImage> m_frames;
};
