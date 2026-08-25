#pragma once
#include "AppConfig.h"
#include "ThumbnailProvider.h"
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QVector>
#include <atomic>
#include <cstdint>
#include <memory>

class DriscordCore;
class QVideoSink;

// Wraps DriscordCore — all methods callable from QML via Q_INVOKABLE.
// Callbacks from the core are forwarded to the main thread via QMetaObject::invokeMethod.
class DriscordBridge : public QObject {
    Q_OBJECT
public:
    explicit DriscordBridge(QObject* parent = nullptr,
        const QVector<IceServerSetting>& iceServers = { });
    ~DriscordBridge();

    void setThumbnailProvider(ThumbnailProvider* tp);

    // Stops media and detaches the image providers. The QML engine owns those
    // providers and is destroyed before this object, so the media threads must
    // be joined here rather than in the destructor.
    void shutdown();

    // Transport
    Q_INVOKABLE void connect(const QString& serverUrl,
        const QString& username,
        const QString& accessToken);
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE bool connected() const;
    Q_INVOKABLE QString localId() const;
    Q_INVOKABLE QString voiceStatsJson() const;

    // Audio
    Q_INVOKABLE void audioStart();
    Q_INVOKABLE void audioStop();
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE bool muted() const;
    Q_INVOKABLE void setDeafened(bool deafened);
    Q_INVOKABLE bool deafened() const;
    Q_INVOKABLE void setMasterVolume(float vol);
    Q_INVOKABLE float masterVolume() const;
    Q_INVOKABLE QVariantList listInputDevices() const;
    Q_INVOKABLE QVariantList listOutputDevices() const;
    Q_INVOKABLE bool setInputDevice(const QString& id);
    Q_INVOKABLE bool setOutputDevice(const QString& id);
    Q_INVOKABLE QString currentInputDevice() const;
    Q_INVOKABLE QString currentOutputDevice() const;
    Q_INVOKABLE void setPeerVolume(const QString& peerId, float vol);
    Q_INVOKABLE float peerVolume(const QString& peerId) const;
    Q_INVOKABLE void setPeerMuted(const QString& peerId, bool muted);
    Q_INVOKABLE bool peerMuted(const QString& peerId) const;

    // Video / Screen sharing
    Q_INVOKABLE void initScreenSession();
    Q_INVOKABLE void deinitScreenSession();
    Q_INVOKABLE QString captureVideoTargetsJson() const;
    Q_INVOKABLE QString captureAudioTargetsJson() const;
    // Capturing one frame of a source can block for up to two seconds, so the
    // answer arrives on thumbnailReady instead of blocking the UI thread once
    // per source.
    Q_INVOKABLE void requestThumbnail(const QString& targetJson, int maxW, int maxH);
    Q_INVOKABLE bool startSharing(const QString& targetJson,
        int maxW,
        int maxH,
        int fps,
        bool audio,
        const QString& audioTarget = { });
    Q_INVOKABLE void stopSharing();
    Q_INVOKABLE bool sharing() const;
    Q_INVOKABLE void setLocalPreviewEnabled(bool enabled);
    // Per-peer video sinks: QML VideoOutput hands its videoSink here while a
    // tile or the expanded view is on screen. Decoded I420 frames are wrapped
    // into QVideoFrame(Format_YUV420P) on the decoder thread and handed to
    // QVideoSink::setVideoFrame (thread-safe); YUV->RGB happens in the Qt RHI
    // shader at render time. Unregister before the VideoOutput dies; the
    // bridge also drops sinks on their destroyed() as a safety net.
    Q_INVOKABLE void registerVideoSink(const QString& peerId, QVideoSink* sink);
    Q_INVOKABLE void unregisterVideoSink(const QString& peerId, QVideoSink* sink);

    Q_INVOKABLE void joinStream(const QString& peerId);
    Q_INVOKABLE void leaveStream(const QString& peerId);
    Q_INVOKABLE QString screenStatsJson(const QString& peerId) const;
    Q_INVOKABLE void setStreamVolume(const QString& peerId, float vol);

signals:
    void wsConnected();
    void wsDisconnected();
    void peerJoined(const QString& peerId);
    void peerLeft(const QString& peerId);
    void peerIdentityReceived(const QString& peerId, const QString& username);
    void streamingStarted(const QString& peerId);
    void streamingStopped(const QString& peerId);
    void streamWatchRejected(const QString& peerId, const QString& reason);
    void frameRemoved(const QString& peerId);
    // Empty url means the source could not be captured.
    void thumbnailReady(const QString& targetJson, const QString& url);

private:
    std::unique_ptr<DriscordCore> m_core;
    ThumbnailProvider* m_thumbnailProvider = nullptr;
    // Written from the GUI thread, read (and used) from the decoder thread
    // under the same lock, so a sink can never be destroyed mid-delivery as
    // long as QML unregisters before its VideoOutput goes away.
    QMutex m_sinkMutex;
    QHash<QString, QList<QVideoSink*>> m_videoSinks;
    // Audio-device discovery can block. Keeping it in an owned, serial pool
    // lets shutdown join it and prevents a late voice_start() after leave().
    QThreadPool m_audioPool;
    std::atomic<std::uint64_t> m_audioGeneration { 0 };
    // One thread: desktop capture is serialised anyway, and a burst of source
    // previews should not fan out into as many X11 capturers.
    QThreadPool m_thumbnailPool;
};
