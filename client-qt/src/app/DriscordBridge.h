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

class DriscordBridge : public QObject {
    Q_OBJECT
public:
    explicit DriscordBridge(QObject* parent = nullptr,
        const QVector<IceServerSetting>& iceServers = { });
    ~DriscordBridge();

    void setThumbnailProvider(ThumbnailProvider* tp);

    void shutdown();

    Q_INVOKABLE void connect(const QString& serverUrl,
        const QString& username,
        const QString& accessToken);
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE bool connected() const;
    Q_INVOKABLE QString localId() const;
    Q_INVOKABLE QString voiceStatsJson() const;

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

    Q_INVOKABLE void initScreenSession();
    Q_INVOKABLE void deinitScreenSession();
    Q_INVOKABLE QString captureVideoTargetsJson() const;
    Q_INVOKABLE QString captureAudioTargetsJson() const;
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
    void thumbnailReady(const QString& targetJson, const QString& url);

private:
    std::unique_ptr<DriscordCore> m_core;
    ThumbnailProvider* m_thumbnailProvider = nullptr;
    QMutex m_sinkMutex;
    QHash<QString, QList<QVideoSink*>> m_videoSinks;
    QThreadPool m_audioPool;
    std::atomic<std::uint64_t> m_audioGeneration { 0 };
    QThreadPool m_thumbnailPool;
};
