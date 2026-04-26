#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include "FrameProvider.h"
#include "ThumbnailProvider.h"

// Wraps DriscordCore — all methods callable from QML via Q_INVOKABLE.
// Callbacks from the core are forwarded to the main thread via QMetaObject::invokeMethod.
class DriscordBridge : public QObject {
    Q_OBJECT
public:
    explicit DriscordBridge(QObject* parent = nullptr);
    ~DriscordBridge();

    void setFrameProvider(FrameProvider* fp);
    void setThumbnailProvider(ThumbnailProvider* tp);

    // Transport
    Q_INVOKABLE void addTurnServer(const QString& url, const QString& user, const QString& pass);
    Q_INVOKABLE void connect(const QString& serverUrl, const QString& username);
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE bool connected() const;
    Q_INVOKABLE QString localId() const;
    Q_INVOKABLE QString peersJson() const;
    Q_INVOKABLE QString transportStatsJson() const;

    // Audio
    Q_INVOKABLE void audioStart();
    Q_INVOKABLE void audioStop();
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE bool muted() const;
    Q_INVOKABLE void setDeafened(bool deafened);
    Q_INVOKABLE bool deafened() const;
    Q_INVOKABLE void setMasterVolume(float vol);
    Q_INVOKABLE float masterVolume() const;
    Q_INVOKABLE float inputLevel() const;
    Q_INVOKABLE float outputLevel() const;
    Q_INVOKABLE void setNoiseGate(float threshold);
    Q_INVOKABLE QStringList listInputDevices() const;
    Q_INVOKABLE QStringList listOutputDevices() const;
    Q_INVOKABLE void setInputDevice(const QString& id);
    Q_INVOKABLE void setOutputDevice(const QString& id);
    Q_INVOKABLE void setPeerVolume(const QString& peerId, float vol);
    Q_INVOKABLE float peerVolume(const QString& peerId) const;
    Q_INVOKABLE void setPeerMuted(const QString& peerId, bool muted);
    Q_INVOKABLE bool peerMuted(const QString& peerId) const;

    // Video / Screen sharing
    Q_INVOKABLE void initScreenSession();
    Q_INVOKABLE void deinitScreenSession();
    Q_INVOKABLE QString captureVideoTargetsJson() const;
    Q_INVOKABLE QString captureAudioTargetsJson() const;
    Q_INVOKABLE QString grabThumbnail(const QString& targetJson, int maxW, int maxH);
    Q_INVOKABLE void startSharing(const QString& targetJson, int maxW, int maxH, int fps, bool audio);
    Q_INVOKABLE void stopSharing();
    Q_INVOKABLE bool sharing() const;
    Q_INVOKABLE void setVideoWatching(bool watching);
    Q_INVOKABLE bool videoWatching() const;
    Q_INVOKABLE void joinStream(const QString& peerId);
    Q_INVOKABLE void leaveStream();
    Q_INVOKABLE QString screenStatsJson() const;
    Q_INVOKABLE void setStreamVolume(const QString& peerId, float vol);
    Q_INVOKABLE float streamVolume() const;

signals:
    void wsConnected();
    void wsDisconnected();
    void peerJoined(const QString& peerId);
    void peerLeft(const QString& peerId);
    void peerIdentityReceived(const QString& peerId, const QString& username);
    void streamingStarted(const QString& peerId);
    void streamingStopped(const QString& peerId);
    void newStreamingPeer(const QString& peerId);
    void streamingPeerRemoved(const QString& peerId);
    void frameRemoved(const QString& peerId);
    void frameUpdated(const QString& peerId);

private:
    FrameProvider*     m_frameProvider     = nullptr;
    ThumbnailProvider* m_thumbnailProvider = nullptr;
    // Drives ScreenSession::update() at ~60Hz so decoded video frames are
    // delivered to on_frame_cb_. Without this tick, the receiver decodes
    // chunks but never hands frames to the UI.
    QTimer* m_screenTickTimer = nullptr;
};
