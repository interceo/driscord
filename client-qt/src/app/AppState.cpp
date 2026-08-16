#include "AppState.h"
#include "DriscordBridge.h"
#include "api/AuthManager.h"
#include "api/ServerRepository.h"
#include "api/UserRepository.h"
#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace {

QString avatarUrlFor(const QJsonObject& user, const QString& apiBaseUrl,
    int userId, bool cacheBust = false)
{
    const auto value = user.value(QStringLiteral("avatar_url"));
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        return { };
    }
    QString result
        = QStringLiteral("%1/users/%2/avatar").arg(apiBaseUrl).arg(userId);
    if (cacheBust) {
        result += QStringLiteral("?t=%1")
                      .arg(QDateTime::currentMSecsSinceEpoch());
    }
    return result;
}

} // namespace

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
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(2000);
    connect(m_statsTimer, &QTimer::timeout, this, [this] { pollConnectionStats(); });

    connectBridgeSignals();

    connect(m_auth, &AuthManager::authChanged, this, [this] {
        if (m_auth->loggedIn()) {
            loadInitialData();
        } else {
            // Logging out must also tear down media. Merely swapping the QML
            // screen would otherwise leave the microphone and WebSocket alive
            // behind the login form.
            leaveVoiceChannel();
            m_userProfile = { };
            m_servers = { };
            m_channels = { };
            m_users = { };
            m_peers = { };
            m_streamingPeers = { };
            m_watchedPeerIds = { };
            m_selectedServerId = -1;
            m_selectedChannelId = -1;
            emit userProfileChanged();
            emit serversChanged();
            emit channelsChanged();
            emit usersChanged();
            emit peersChanged();
            emit streamingPeersChanged();
            emit watchedStreamsChanged();
            emit selectedServerChanged();
            emit selectedChannelChanged();
        }
    });
    connect(m_auth, &AuthManager::loginError, this, [this](const QString& msg) {
        setApiError(msg);
    });
}

void AppState::connectBridgeSignals()
{
    connect(m_bridge, &DriscordBridge::wsConnected, this, [this] {
        if (!m_auth->loggedIn() || m_selectedChannelId < 0) {
            m_bridge->disconnect();
            return;
        }
        m_connectionState = QStringLiteral("connected");
        m_statsTimer->start();
        emit connectionChanged();
    });
    connect(m_bridge, &DriscordBridge::wsDisconnected, this, [this] {
        const bool sessionWasActive
            = m_connectionState != QStringLiteral("disconnected");
        m_connectionState = QStringLiteral("disconnected");
        m_statsTimer->stop();
        // A remote/network disconnect does not pass through
        // leaveVoiceChannel(). Stop local capture here as well. During an
        // intentional leave the state is already disconnected by the time
        // this queued signal arrives, so the teardown is not duplicated.
        if (sessionWasActive) {
            m_bridge->audioStop();
            m_bridge->deinitScreenSession();
        }
        resetConnectionStats();
        if (!m_peers.isEmpty()) {
            m_peers.clear();
            emit peersChanged();
        }
        if (!m_streamingPeers.isEmpty()) {
            m_streamingPeers.clear();
            emit streamingPeersChanged();
        }
        if (!m_watchedPeerIds.isEmpty()) {
            m_watchedPeerIds.clear();
            emit watchedStreamsChanged();
        }
        emit sharingChanged();
        emit connectionChanged();
    });

    connect(m_bridge, &DriscordBridge::peerJoined, this, [this](const QString& id) {
        QVariantMap peer {
            { "id", id }, { "username", "" }, { "displayName", "" }, { "avatarUrl", "" }
        };
        m_peers.append(peer);
        emit peersChanged();
    });
    connect(m_bridge, &DriscordBridge::peerLeft, this, [this](const QString& id) {
        m_peers.removeIf([&id](const QVariant& v) {
            return v.toMap().value("id").toString() == id;
        });
        emit peersChanged();
        if (m_streamingPeers.removeAll(id)) {
            emit streamingPeersChanged();
        }
        if (m_watchedPeerIds.removeAll(id)) {
            emit watchedStreamsChanged();
        }
    });
    connect(m_bridge, &DriscordBridge::peerIdentityReceived, this,
        [this](const QString& id, const QString& username) {
            const int requestedUserId = m_auth->userId();
            for (auto& v : m_peers) {
                auto m = v.toMap();
                if (m.value("id") == id) {
                    m["username"] = username;
                    v = m;
                    // Fetch avatar + display name
                    m_userRepo->getUserByUsername(username, [this, id, requestedUserId](bool ok, QJsonObject json) {
                        if (!ok || !m_auth->loggedIn()
                            || m_auth->userId() != requestedUserId)
                            return;
                        int uid = json["id"].toInt();
                        const QString avatarUrl
                            = avatarUrlFor(json, m_apiBaseUrl, uid);
                        QString displayName = json["display_name"].toString();
                        if (displayName.isEmpty())
                            displayName = json["username"].toString();
                        for (auto& pv : m_peers) {
                            auto pm = pv.toMap();
                            if (pm.value("id") == id) {
                                pm["userId"] = uid;
                                pm["avatarUrl"] = avatarUrl;
                                pm["displayName"] = displayName;
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

    auto addStreamingPeer = [this](const QString& id) {
        if (m_streamingPeers.contains(id))
            return;
        m_streamingPeers.append(id);
        emit streamingPeersChanged();
    };
    auto removeStreamingPeer = [this](const QString& id) {
        if (!m_streamingPeers.removeAll(id))
            return;
        emit streamingPeersChanged();
        if (m_watchedPeerIds.removeAll(id)) {
            emit watchedStreamsChanged();
        }
    };
    // Signaling announces streams before the first decoded WebRTC frame, so a
    // tile can subscribe and display a buffering state immediately.
    connect(m_bridge, &DriscordBridge::streamingStarted, this, addStreamingPeer);
    connect(m_bridge, &DriscordBridge::streamingStopped, this, removeStreamingPeer);
    connect(m_bridge, &DriscordBridge::streamWatchRejected, this,
        [this](const QString& id, const QString& reason) {
            if (m_watchedPeerIds.removeAll(id)) {
                emit watchedStreamsChanged();
            }
            if (reason == QStringLiteral("capacity")) {
                setApiError(tr("No free screen-stream slot. Stop watching another stream and try again."));
            } else if (reason == QStringLiteral("not_streaming")) {
                setApiError(tr("This stream has already stopped."));
            } else {
                setApiError(tr("This stream is no longer available."));
            }
        });
}

void AppState::loadInitialData()
{
    int uid = m_auth->userId();
    QVariantMap p;
    p["id"] = uid;
    p["username"] = m_auth->username();
    p["displayName"] = m_auth->displayName();
    if (!m_auth->avatarUrl().isEmpty())
        p["avatarUrl"] = QStringLiteral("%1/users/%2/avatar").arg(m_apiBaseUrl).arg(uid);
    else
        p["avatarUrl"] = QString();
    m_userProfile = p;
    emit userProfileChanged();

    fetchCurrentUserProfile();
    reloadServers();
    emit connectionChanged();
}

void AppState::fetchCurrentUserProfile()
{
    const int requestedUserId = m_auth->userId();
    if (requestedUserId <= 0)
        return;
    // Use /users/me so we get private fields (email) — /users/{id} omits them.
    m_userRepo->getMe([this, requestedUserId](bool ok, QJsonObject json) {
        // A reply from the previous session must never repopulate the profile
        // after logout or a different user has logged in.
        if (!ok || !m_auth->loggedIn() || m_auth->userId() != requestedUserId)
            return;
        QVariantMap p;
        p["id"] = json["id"].toInt();
        p["username"] = json["username"].toString();
        QString dn = json["display_name"].toString();
        p["displayName"] = dn.isEmpty() ? json["username"].toString() : dn;
        p["email"] = json["email"].toString();
        int uid = json["id"].toInt();
        const QString avatarUrl
            = avatarUrlFor(json, m_apiBaseUrl, uid, true);
        p["avatarUrl"] = avatarUrl;
        m_userProfile = p;
        emit userProfileChanged();
    });
}

void AppState::reloadServers()
{
    const int requestedUserId = m_auth->userId();
    m_serverRepo->listServers([this, requestedUserId](bool ok, QJsonArray arr) {
        if (!ok || !m_auth->loggedIn()
            || m_auth->userId() != requestedUserId)
            return;
        m_servers.clear();
        for (const auto& v : arr) {
            auto o = v.toObject();
            m_servers.append(QVariantMap {
                { "id", o["id"].toInt() },
                { "name", o["name"].toString() },
                { "ownerId", o["owner_id"].toInt() },
            });
        }
        emit serversChanged();
    });
}

void AppState::reloadChannels()
{
    if (m_selectedServerId < 0)
        return;
    const int requestedUserId = m_auth->userId();
    const int requestedServerId = m_selectedServerId;
    m_serverRepo->listChannels(requestedServerId,
        [this, requestedUserId, requestedServerId](bool ok, QJsonArray arr) {
            // Server A may finish after the user has already selected server
            // B. Discard that response instead of rendering A's channels
            // under B's title.
            if (!ok || !m_auth->loggedIn()
                || m_auth->userId() != requestedUserId
                || m_selectedServerId != requestedServerId)
                return;
            m_channels.clear();
            for (const auto& v : arr) {
                auto o = v.toObject();
                m_channels.append(QVariantMap {
                    { "id", o["id"].toInt() },
                    { "name", o["name"].toString() },
                    { "kind", o["kind"].toString() },
                });
            }
            emit channelsChanged();
        });
}

void AppState::setApiError(const QString& e)
{
    m_apiError = e;
    emit apiErrorChanged();
}

bool AppState::connected() const { return m_bridge->connected(); }
QString AppState::localId() const { return m_bridge->localId(); }
bool AppState::muted() const { return m_bridge->muted(); }
bool AppState::deafened() const { return m_bridge->deafened(); }
bool AppState::sharing() const { return m_bridge->sharing(); }

bool AppState::canManageSelectedServer() const
{
    for (const auto& value : m_servers) {
        const auto server = value.toMap();
        if (server.value("id").toInt() == m_selectedServerId)
            return server.value("ownerId").toInt() == m_auth->userId();
    }
    return false;
}

void AppState::selectServer(int id)
{
    // This state model has one selected channel and uses it for the active
    // voice connection. Keeping a connection to the old server after replacing
    // that id makes the voice banner and peer list internally inconsistent.
    if (id != m_selectedServerId
        && m_connectionState != QStringLiteral("disconnected")) {
        leaveVoiceChannel();
    }
    m_selectedServerId = id;
    m_selectedChannelId = -1;
    m_channels.clear();
    emit selectedServerChanged();
    emit selectedChannelChanged();
    emit channelsChanged();
    reloadChannels();
}

void AppState::selectChannel(int id)
{
    m_selectedChannelId = id;
    emit selectedChannelChanged();
}

void AppState::joinVoiceChannel(int channelId)
{
    const auto channel = std::find_if(m_channels.cbegin(), m_channels.cend(),
        [channelId](const QVariant& value) {
            const auto item = value.toMap();
            return item.value("id").toInt() == channelId
                && item.value("kind").toString() == QStringLiteral("voice");
        });
    if (channel == m_channels.cend()) {
        setApiError(tr("The selected voice channel is no longer available."));
        return;
    }
    if (m_connectionState != QStringLiteral("disconnected")) {
        leaveVoiceChannel();
    }
    m_selectedChannelId = channelId;
    emit selectedChannelChanged();
    m_connectionState = QStringLiteral("connecting");
    emit connectionChanged();
    m_bridge->initScreenSession();
    // The signaling server reads the channel id from the WebSocket URL path
    // (/channels/<id>) and uses it as the room key — peers are only visible
    // to one another within the same room.
    const QString url = m_signalingUrl + QStringLiteral("/channels/%1").arg(channelId);
    m_bridge->connect(url, m_auth->username(), m_auth->accessToken());
    m_bridge->audioStart();
}

void AppState::leaveVoiceChannel()
{
    m_statsTimer->stop();
    m_bridge->audioStop();
    m_bridge->deinitScreenSession();
    m_bridge->disconnect();
    m_connectionState = QStringLiteral("disconnected");
    m_peers.clear();
    m_streamingPeers.clear();
    m_watchedPeerIds.clear();
    resetConnectionStats();
    emit peersChanged();
    emit streamingPeersChanged();
    emit watchedStreamsChanged();
    emit sharingChanged();
    emit connectionChanged();
}

void AppState::setMuted(bool m)
{
    m_bridge->setMuted(m);
    emit audioStateChanged();
}
void AppState::setDeafened(bool d)
{
    m_bridge->setDeafened(d);
    emit audioStateChanged();
}
void AppState::setMasterVolume(float v) { m_bridge->setMasterVolume(v); }

bool AppState::startSharing(const QString& tj, int w, int h, int fps, bool audio,
    const QString& audioTarget)
{
    const bool started
        = m_bridge->startSharing(tj, w, h, fps, audio, audioTarget);
    emit sharingChanged();
    if (!started) {
        setApiError(tr("Failed to start screen sharing. The selected source may no longer be available."));
    }
    return started;
}
void AppState::stopSharing()
{
    m_bridge->stopSharing();
    emit sharingChanged();
}
void AppState::joinStream(const QString& id)
{
    if (m_watchedPeerIds.contains(id))
        return;
    m_bridge->joinStream(id);
    m_watchedPeerIds.append(id);
    emit watchedStreamsChanged();
}
void AppState::leaveStream(const QString& id)
{
    if (m_watchedPeerIds.removeAll(id)) {
        m_bridge->leaveStream(id);
        emit watchedStreamsChanged();
    }
}

bool AppState::isWatchingStream(const QString& id) const
{
    return m_watchedPeerIds.contains(id);
}

void AppState::createServer(const QString& name, const QString& desc)
{
    const int requestedUserId = m_auth->userId();
    m_serverRepo->createServer(name, desc, [this, requestedUserId](bool ok, QJsonObject) {
        if (!m_auth->loggedIn() || m_auth->userId() != requestedUserId)
            return;
        if (ok)
            reloadServers();
        else
            setApiError("Failed to create server");
    });
}

void AppState::createChannel(const QString& name, const QString& kind)
{
    if (m_selectedServerId < 0)
        return;
    const int requestedUserId = m_auth->userId();
    const int requestedServerId = m_selectedServerId;
    m_serverRepo->createChannel(requestedServerId, name, kind,
        [this, requestedUserId, requestedServerId](bool ok, QJsonObject) {
            if (!m_auth->loggedIn() || m_auth->userId() != requestedUserId)
                return;
            if (ok) {
                if (m_selectedServerId == requestedServerId)
                    reloadChannels();
            } else {
                setApiError("Failed to create channel");
            }
        });
}

void AppState::acceptInvite(const QString& code)
{
    const int requestedUserId = m_auth->userId();
    m_serverRepo->acceptInvite(code, [this, requestedUserId](bool ok, QJsonObject json) {
        if (!m_auth->loggedIn() || m_auth->userId() != requestedUserId)
            return;
        if (ok) {
            reloadServers();
            selectServer(json["server_id"].toInt());
        } else
            setApiError("Invalid invite code");
    });
}

void AppState::loadUsers()
{
    const int requestedUserId = m_auth->userId();
    m_userRepo->listUsers([this, requestedUserId](bool ok, QJsonArray users) {
        if (!m_auth->loggedIn() || m_auth->userId() != requestedUserId)
            return;
        if (!ok) {
            setApiError("Failed to load users");
            return;
        }

        m_users.clear();
        for (const auto& value : users) {
            const auto user = value.toObject();
            const int userId = user["id"].toInt();
            if (userId == m_auth->userId())
                continue;

            QString displayName = user["display_name"].toString();
            if (displayName.isEmpty())
                displayName = user["username"].toString();
            const QString avatarUrl
                = avatarUrlFor(user, m_apiBaseUrl, userId);

            m_users.append(QVariantMap {
                { "id", userId },
                { "username", user["username"].toString() },
                { "displayName", displayName },
                { "avatarUrl", avatarUrl },
            });
        }
        emit usersChanged();
    });
}

void AppState::inviteUser(int userId)
{
    if (!canManageSelectedServer()) {
        setApiError("Only the server owner can invite users");
        return;
    }
    const int requestedUserId = m_auth->userId();
    const int requestedServerId = m_selectedServerId;
    m_serverRepo->addMember(requestedServerId, userId,
        [this, userId, requestedUserId, requestedServerId](bool ok, QJsonObject) {
            if (!m_auth->loggedIn() || m_auth->userId() != requestedUserId
                || m_selectedServerId != requestedServerId)
                return;
            if (!ok) {
                setApiError("Failed to add user; they may already be a member");
                return;
            }
            m_users.removeIf([userId](const QVariant& value) {
                return value.toMap().value("id").toInt() == userId;
            });
            emit usersChanged();
        });
}

void AppState::updateDisplayName(const QString& name)
{
    int uid = m_userProfile.value("id").toInt();
    m_userRepo->updateProfile(uid, name, [this](bool ok, QJsonObject) {
        if (ok)
            fetchCurrentUserProfile();
    });
}

void AppState::uploadAvatarFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QFile::ReadOnly))
        return;
    QString ext = QFileInfo(path).suffix().toLower();
    uploadAvatar(f.readAll(), ext);
}

void AppState::uploadAvatar(const QByteArray& data, const QString& ext)
{
    int uid = m_userProfile.value("id").toInt();
    m_userRepo->uploadAvatar(uid, data, ext, [this](bool ok, QJsonObject) {
        if (ok)
            fetchCurrentUserProfile();
    });
}

void AppState::uploadAvatarCropped(const QString& imagePath, qreal scale, qreal offsetX, qreal offsetY)
{
    QString localPath = imagePath.startsWith("file://") ? QUrl(imagePath).toLocalFile() : imagePath;
    QImage src(localPath);
    if (src.isNull())
        return;

    int scaledW = qRound(src.width() * scale);
    int scaledH = qRound(src.height() * scale);
    QImage scaled = src.scaled(scaledW, scaledH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    constexpr int OUT = 256;
    int drawX = qRound(offsetX + (OUT - scaledW) / 2.0);
    int drawY = qRound(offsetY + (OUT - scaledH) / 2.0);

    QImage result(OUT, OUT, QImage::Format_RGB32);
    result.fill(QColor("#1e1f22"));
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(drawX, drawY, scaled);
    p.end();

    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    result.save(&buf, "PNG");
    uploadAvatar(ba, "png");
}

QString AppState::captureVideoTargetsJson() const
{
    return m_bridge->captureVideoTargetsJson();
}

void AppState::resetConnectionStats()
{
    m_avgRttMs = -1;
    m_lastRttMs = -1;
    m_packetsLost = 0;
    m_remoteVoiceTracks = 0;
    m_rttHistory.clear();
    emit connectionStatsChanged();
}

void AppState::pollConnectionStats()
{
    // One transport, one round trip: the client talks only to the SFU, so a
    // per-peer latency would be an invention.
    const auto stats
        = QJsonDocument::fromJson(m_bridge->voiceStatsJson().toUtf8()).object();

    m_lastRttMs = stats.value(QStringLiteral("rttMs")).toInt(-1);
    m_packetsLost = stats.value(QStringLiteral("packetsLost")).toInt(0);
    m_remoteVoiceTracks
        = stats.value(QStringLiteral("remoteTracks")).toInt(0);

    QVariantMap sample;
    sample["t"] = QDateTime::currentMSecsSinceEpoch();
    sample["rtt"] = m_lastRttMs;
    m_rttHistory.append(sample);
    constexpr int kMaxSamples = 60;
    while (m_rttHistory.size() > kMaxSamples)
        m_rttHistory.removeFirst();

    int sum = 0;
    int measured = 0;
    for (const auto& value : m_rttHistory) {
        const int rtt = value.toMap().value("rtt").toInt();
        if (rtt < 0)
            continue;
        sum += rtt;
        ++measured;
    }
    m_avgRttMs = measured > 0 ? sum / measured : -1;

    emit connectionStatsChanged();
}
