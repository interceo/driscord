#include "AppState.h"
#include "DriscordBridge.h"
#include "api/AuthManager.h"
#include "api/ServerRepository.h"
#include "api/UserRepository.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

AppState::AppState(AuthManager* auth, ServerRepository* servers, UserRepository* users,
                   DriscordBridge* bridge, const QString& signalingUrl,
                   const QString& apiBaseUrl, QObject* parent)
    : QObject(parent)
    , m_auth(auth)
    , m_serverRepo(servers)
    , m_userRepo(users)
    , m_bridge(bridge)
    , m_signalingUrl(signalingUrl)
    , m_apiBaseUrl(apiBaseUrl)
{
    m_levelTimer = new QTimer(this);
    m_levelTimer->setInterval(50);
    connect(m_levelTimer, &QTimer::timeout, this, [this] { emit audioLevelsChanged(); });

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(2000);
    connect(m_statsTimer, &QTimer::timeout, this, [this] { pollConnectionStats(); });

    connectBridgeSignals();

    connect(m_auth, &AuthManager::sessionRestored, this, [this] { loadInitialData(); });
    connect(m_auth, &AuthManager::authChanged, this, [this] {
        if (m_auth->loggedIn()) {
            loadInitialData();
        } else {
            m_userProfile = {};
            m_servers = {};
            m_channels = {};
            m_peers = {};
            emit userProfileChanged();
            emit serversChanged();
        }
    });
    connect(m_auth, &AuthManager::loginError, this, [this](const QString& msg) {
        setApiError(msg);
    });
}

void AppState::connectBridgeSignals() {
    connect(m_bridge, &DriscordBridge::wsConnected,    this, [this] {
        m_connectionState = QStringLiteral("connected");
        m_statsTimer->start();
        emit connectionChanged();
    });
    connect(m_bridge, &DriscordBridge::wsDisconnected, this, [this] {
        m_connectionState = QStringLiteral("disconnected");
        m_statsTimer->stop();
        resetConnectionStats();
        emit connectionChanged();
    });

    connect(m_bridge, &DriscordBridge::peerJoined, this, [this](const QString& id) {
        QVariantMap peer{{"id", id}, {"username", ""}, {"avatarUrl", ""}};
        m_peers.append(peer);
        emit peersChanged();
        m_bridge->audioStart();
    });
    connect(m_bridge, &DriscordBridge::peerLeft, this, [this](const QString& id) {
        m_peers.removeIf([&id](const QVariant& v) {
            return v.toMap().value("id").toString() == id;
        });
        emit peersChanged();
    });
    connect(m_bridge, &DriscordBridge::peerIdentityReceived, this,
            [this](const QString& id, const QString& username) {
        for (auto& v : m_peers) {
            auto m = v.toMap();
            if (m.value("id") == id) {
                m["username"] = username;
                v = m;
                // Fetch avatar
                m_userRepo->getUserByUsername(username, [this, id](bool ok, QJsonObject json) {
                    if (!ok) return;
                    int uid = json["id"].toInt();
                    QString avatarUrl;
                    if (!json["avatar_url"].isNull())
                        avatarUrl = m_apiBaseUrl
                                    + QStringLiteral("/users/%1/avatar").arg(uid);
                    for (auto& pv : m_peers) {
                        auto pm = pv.toMap();
                        if (pm.value("id") == id) {
                            pm["userId"]    = uid;
                            pm["avatarUrl"] = avatarUrl;
                            pv = pm;
                            break;
                        }
                    }
                    emit peersChanged();
                });
                break;
            }
        }
        emit peersChanged();
    });

    connect(m_bridge, &DriscordBridge::streamingStarted,      this, &AppState::sharingChanged);
    connect(m_bridge, &DriscordBridge::streamingStopped,      this, &AppState::sharingChanged);
    connect(m_bridge, &DriscordBridge::newStreamingPeer,      this, [this](const QString& id) {
        if (!m_streamingPeers.contains(id)) m_streamingPeers.append(id);
        emit streamingPeersChanged();
    });
    connect(m_bridge, &DriscordBridge::streamingPeerRemoved,  this, [this](const QString& id) {
        m_streamingPeers.removeAll(id);
        emit streamingPeersChanged();
    });
}

void AppState::loadInitialData() {
    int uid = m_auth->userId();
    QVariantMap p;
    p["id"]          = uid;
    p["username"]    = m_auth->username();
    p["displayName"] = m_auth->username();
    if (!m_auth->avatarUrl().isEmpty())
        p["avatarUrl"] = QStringLiteral("%1/users/%2/avatar").arg(m_apiBaseUrl).arg(uid);
    else
        p["avatarUrl"] = QString();
    m_userProfile = p;
    emit userProfileChanged();

    fetchCurrentUserProfile();
    reloadServers();
    m_levelTimer->start();
    emit connectionChanged();
}

void AppState::fetchCurrentUserProfile() {
    int uid = m_auth->userId();
    if (uid <= 0) return;
    m_userRepo->getUserById(uid, [this](bool ok, QJsonObject json) {
        if (!ok) return;
        QVariantMap p;
        p["id"]          = json["id"].toInt();
        p["username"]    = json["username"].toString();
        p["displayName"] = json["display_name"].toString();
        int uid = json["id"].toInt();
        QString avatarUrl;
        if (!json["avatar_url"].isNull())
            avatarUrl = QStringLiteral("%1/users/%2/avatar")
                        .arg(m_apiBaseUrl).arg(uid);
        p["avatarUrl"] = avatarUrl;
        m_userProfile = p;
        emit userProfileChanged();
    });
}

void AppState::reloadServers() {
    m_serverRepo->listServers([this](bool ok, QJsonArray arr) {
        if (!ok) return;
        m_servers.clear();
        for (const auto& v : arr) {
            auto o = v.toObject();
            m_servers.append(QVariantMap{
                {"id",   o["id"].toInt()},
                {"name", o["name"].toString()},
            });
        }
        emit serversChanged();
    });
}

void AppState::reloadChannels() {
    if (m_selectedServerId < 0) return;
    m_serverRepo->listChannels(m_selectedServerId, [this](bool ok, QJsonArray arr) {
        if (!ok) return;
        m_channels.clear();
        for (const auto& v : arr) {
            auto o = v.toObject();
            m_channels.append(QVariantMap{
                {"id",   o["id"].toInt()},
                {"name", o["name"].toString()},
                {"kind", o["kind"].toString()},
            });
        }
        emit channelsChanged();
    });
}

void AppState::setApiError(const QString& e) { m_apiError = e; emit apiErrorChanged(); }

bool  AppState::connected()   const { return m_bridge->connected(); }
QString AppState::localId()   const { return m_bridge->localId(); }
bool  AppState::muted()       const { return m_bridge->muted(); }
bool  AppState::deafened()    const { return m_bridge->deafened(); }
float AppState::inputLevel()  const { return m_bridge->inputLevel(); }
float AppState::outputLevel() const { return m_bridge->outputLevel(); }
bool  AppState::sharing()     const { return m_bridge->sharing(); }

void AppState::selectServer(int id) {
    m_selectedServerId = id;
    m_selectedChannelId = -1;
    emit selectedServerChanged();
    reloadChannels();
}

void AppState::selectChannel(int id) {
    m_selectedChannelId = id;
    emit selectedChannelChanged();
}

void AppState::joinVoiceChannel(int channelId) {
    m_selectedChannelId = channelId;
    emit selectedChannelChanged();
    m_connectionState = QStringLiteral("connecting");
    emit connectionChanged();
    m_bridge->initScreenSession();
    // The signaling server reads the channel id from the WebSocket URL path
    // (/channels/<id>) and uses it as the room key — peers are only visible
    // to one another within the same room.
    const QString url = m_signalingUrl + QStringLiteral("/channels/%1").arg(channelId);
    m_bridge->connect(url, m_auth->username());
    m_bridge->audioStart();
}

void AppState::leaveVoiceChannel() {
    m_bridge->audioStop();
    m_bridge->deinitScreenSession();
    m_bridge->disconnect();
    m_connectionState = QStringLiteral("disconnected");
    m_peers.clear();
    m_streamingPeers.clear();
    emit peersChanged();
    emit streamingPeersChanged();
    emit connectionChanged();
}

void AppState::setMuted(bool m)      { m_bridge->setMuted(m);      emit audioStateChanged(); }
void AppState::setDeafened(bool d)   { m_bridge->setDeafened(d);   emit audioStateChanged(); }
void AppState::setMasterVolume(float v){ m_bridge->setMasterVolume(v); }

void AppState::startSharing(const QString& tj, int w, int h, int fps, bool audio) {
    m_bridge->startSharing(tj, w, h, fps, audio);
    emit sharingChanged();
}
void AppState::stopSharing()             { m_bridge->stopSharing(); emit sharingChanged(); }
void AppState::joinStream(const QString& id) { m_bridge->joinStream(id); }
void AppState::leaveStream()             { m_bridge->leaveStream(); }

void AppState::createServer(const QString& name, const QString& desc) {
    m_serverRepo->createServer(name, desc, [this](bool ok, QJsonObject) {
        if (ok) reloadServers(); else setApiError("Failed to create server");
    });
}

void AppState::createChannel(const QString& name, const QString& kind) {
    if (m_selectedServerId < 0) return;
    m_serverRepo->createChannel(m_selectedServerId, name, kind, [this](bool ok, QJsonObject) {
        if (ok) reloadChannels(); else setApiError("Failed to create channel");
    });
}

void AppState::acceptInvite(const QString& code) {
    m_serverRepo->acceptInvite(code, [this](bool ok, QJsonObject json) {
        if (ok) { reloadServers(); selectServer(json["server_id"].toInt()); }
        else setApiError("Invalid invite code");
    });
}

void AppState::updateDisplayName(const QString& name) {
    int uid = m_userProfile.value("id").toInt();
    m_userRepo->updateProfile(uid, name, [this](bool ok, QJsonObject) {
        if (ok) fetchCurrentUserProfile();
    });
}

void AppState::uploadAvatarFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) return;
    QString ext = QFileInfo(path).suffix().toLower();
    uploadAvatar(f.readAll(), ext);
}

void AppState::uploadAvatar(const QByteArray& data, const QString& ext) {
    int uid = m_userProfile.value("id").toInt();
    m_userRepo->uploadAvatar(uid, data, ext, [this](bool ok, QJsonObject) {
        if (ok) fetchCurrentUserProfile();
    });
}

QString AppState::captureVideoTargetsJson() const {
    return m_bridge->captureVideoTargetsJson();
}

void AppState::resetConnectionStats() {
    m_avgRttMs = -1;
    m_lastRttMs = -1;
    m_primaryPeerId.clear();
    m_rttHistory.clear();
    emit connectionStatsChanged();
}

void AppState::pollConnectionStats() {
    auto arr = QJsonDocument::fromJson(m_bridge->transportStatsJson().toUtf8()).array();

    int sumRtt = 0;
    int countRtt = 0;
    int firstRtt = -1;
    QString firstId;
    for (const auto& v : arr) {
        auto o = v.toObject();
        int rtt = o.value("rtt_ms").toInt(-1);
        if (rtt < 0) continue;
        if (firstId.isEmpty()) {
            firstId = o.value("id").toString();
            firstRtt = rtt;
        }
        sumRtt += rtt;
        countRtt++;
    }

    m_lastRttMs = firstRtt;
    m_avgRttMs  = countRtt > 0 ? (sumRtt / countRtt) : -1;
    m_primaryPeerId = firstId;

    QVariantMap sample;
    sample["t"] = QDateTime::currentMSecsSinceEpoch();
    sample["rtt"] = m_lastRttMs;
    m_rttHistory.append(sample);
    constexpr int kMaxSamples = 60;
    while (m_rttHistory.size() > kMaxSamples) m_rttHistory.removeFirst();

    emit connectionStatsChanged();
}
