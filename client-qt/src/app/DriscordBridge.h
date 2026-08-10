#pragma once
#include "AppConfig.h"
#include "FrameProvider.h"
#include "ThumbnailProvider.h"
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QVector>

// Wraps DriscordCore — all methods callable from QML via Q_INVOKABLE.
// Callbacks from the core are forwarded to the main thread via QMetaObject::invokeMethod.
class DriscordBridge : public QObject {
    Q_OBJECT
public:
    explicit DriscordBridge(QObject* parent = nullptr,
        const QVector<IceServerSetting>& iceServers = { });
    ~DriscordBridge();

    void setFrameProvider(FrameProvider* fp);
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
    Q_INVOKABLE void startSharing(const QString& targetJson,
        int maxW,
        int maxH,
        int fps,
        bool audio,
        const QString& audioTarget = { });
    Q_INVOKABLE void stopSharing();
    Q_INVOKABLE bool sharing() const;
    Q_INVOKABLE void setLocalPreviewEnabled(bool enabled);
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
    void frameUpdated(const QString& peerId);
    // Empty url means the source could not be captured.
    void thumbnailReady(const QString& targetJson, const QString& url);

private:
    FrameProvider* m_frameProvider = nullptr;
    ThumbnailProvider* m_thumbnailProvider = nullptr;
    // One thread: desktop capture is serialised anyway, and a burst of source
    // previews should not fan out into as many X11 capturers.
    QThreadPool m_thumbnailPool;
};
