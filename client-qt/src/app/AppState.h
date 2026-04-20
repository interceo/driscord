#pragma once
#include <QObject>
#include <QJsonArray>
#include <QVariantList>
#include <QTimer>

class AuthManager;
class ServerRepository;
class UserRepository;
class DriscordBridge;

class AppState : public QObject {
    Q_OBJECT

    // Auth / Profile
    Q_PROPERTY(QVariantMap userProfile READ userProfile NOTIFY userProfileChanged)
    Q_PROPERTY(QString apiError READ apiError WRITE setApiError NOTIFY apiErrorChanged)

    // Servers & Channels
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
    Q_PROPERTY(int selectedServerId READ selectedServerId NOTIFY selectedServerChanged)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(int selectedChannelId READ selectedChannelId NOTIFY selectedChannelChanged)

    // Peers / Connection
    Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionChanged)
    Q_PROPERTY(QString localId READ localId NOTIFY connectionChanged)

    // Audio
    Q_PROPERTY(bool muted     READ muted     NOTIFY audioStateChanged)
    Q_PROPERTY(bool deafened  READ deafened  NOTIFY audioStateChanged)
    Q_PROPERTY(float inputLevel  READ inputLevel  NOTIFY audioLevelsChanged)
    Q_PROPERTY(float outputLevel READ outputLevel NOTIFY audioLevelsChanged)

    // Screen sharing
    Q_PROPERTY(bool sharing READ sharing NOTIFY sharingChanged)
    Q_PROPERTY(QVariantList streamingPeers READ streamingPeers NOTIFY streamingPeersChanged)
    Q_PROPERTY(QString watchedPeerId READ watchedPeerId NOTIFY watchedPeerChanged)

    // Network / call stats (polled while connected)
    Q_PROPERTY(int          avgRttMs    READ avgRttMs    NOTIFY connectionStatsChanged)
    Q_PROPERTY(int          lastRttMs   READ lastRttMs   NOTIFY connectionStatsChanged)
    Q_PROPERTY(QVariantList rttHistory  READ rttHistory  NOTIFY connectionStatsChanged)
    Q_PROPERTY(QString      primaryPeerId READ primaryPeerId NOTIFY connectionStatsChanged)

public:
    explicit AppState(AuthManager*     auth,
                      ServerRepository* servers,
                      UserRepository*   users,
                      DriscordBridge*   bridge,
                      const QString&    signalingUrl,
                      const QString&    apiBaseUrl,
                      QObject* parent = nullptr);

    QVariantMap  userProfile()      const { return m_userProfile; }
    QString      apiError()         const { return m_apiError; }
    QVariantList servers()          const { return m_servers; }
    int          selectedServerId() const { return m_selectedServerId; }
    QVariantList channels()         const { return m_channels; }
    int          selectedChannelId()const { return m_selectedChannelId; }
    QVariantList peers()            const { return m_peers; }
    bool         connected()        const;
    QString      connectionState()  const { return m_connectionState; }
    QString      localId()          const;
    bool         muted()            const;
    bool         deafened()         const;
    float        inputLevel()       const;
    float        outputLevel()      const;
    bool         sharing()          const;
    QVariantList streamingPeers()   const { return m_streamingPeers; }
    int          avgRttMs()         const { return m_avgRttMs; }
    int          lastRttMs()        const { return m_lastRttMs; }
    QVariantList rttHistory()       const { return m_rttHistory; }
    QString      primaryPeerId()    const { return m_primaryPeerId; }
    QString      watchedPeerId()    const { return m_watchedPeerId; }

    void setApiError(const QString& e);

    Q_INVOKABLE void selectServer(int serverId);
    Q_INVOKABLE void selectChannel(int channelId);
    Q_INVOKABLE void joinVoiceChannel(int channelId);
    Q_INVOKABLE void leaveVoiceChannel();
    Q_INVOKABLE void setMuted(bool m);
    Q_INVOKABLE void setDeafened(bool d);
    Q_INVOKABLE void setMasterVolume(float v);
    Q_INVOKABLE void startSharing(const QString& targetJson, int maxW, int maxH, int fps, bool audio);
    Q_INVOKABLE void stopSharing();
    Q_INVOKABLE void joinStream(const QString& peerId);
    Q_INVOKABLE void leaveStream();
    Q_INVOKABLE void createServer(const QString& name, const QString& description);
    Q_INVOKABLE void createChannel(const QString& name, const QString& kind);
    Q_INVOKABLE void acceptInvite(const QString& code);
    Q_INVOKABLE void updateDisplayName(const QString& name);
    Q_INVOKABLE void uploadAvatar(const QByteArray& data, const QString& ext);
    Q_INVOKABLE void uploadAvatarFromFile(const QString& path);
    Q_INVOKABLE void uploadAvatarCropped(const QString& imagePath, qreal scale, qreal offsetX, qreal offsetY);
    Q_INVOKABLE QString captureVideoTargetsJson() const;

    void loadInitialData();

signals:
    void userProfileChanged();
    void apiErrorChanged();
    void serversChanged();
    void selectedServerChanged();
    void channelsChanged();
    void selectedChannelChanged();
    void peersChanged();
    void connectionChanged();
    void audioStateChanged();
    void audioLevelsChanged();
    void sharingChanged();
    void streamingPeersChanged();
    void watchedPeerChanged();
    void connectionStatsChanged();

private:
    void connectBridgeSignals();
    void reloadServers();
    void reloadChannels();
    void fetchCurrentUserProfile();
    void pollConnectionStats();
    void resetConnectionStats();

    AuthManager*      m_auth;
    ServerRepository* m_serverRepo;
    UserRepository*   m_userRepo;
    DriscordBridge*   m_bridge;

    QVariantMap  m_userProfile;
    QString      m_apiError;
    QVariantList m_servers;
    int          m_selectedServerId  = -1;
    QVariantList m_channels;
    int          m_selectedChannelId = -1;
    QVariantList m_peers;
    QVariantList m_streamingPeers;

    QString m_connectionState = QStringLiteral("disconnected"); // disconnected | connecting | connected
    QTimer* m_levelTimer;
    QTimer* m_statsTimer = nullptr;
    int     m_avgRttMs   = -1;
    int     m_lastRttMs  = -1;
    QString m_primaryPeerId;
    QString m_watchedPeerId;
    QVariantList m_rttHistory;       // each entry: {"t": ms, "rtt": int}
    QString m_signalingUrl;
    QString m_apiBaseUrl;
};
