#include "DriscordBridge.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSettings>
#include <QThread>
#include <QThreadPool>
#include <QUrl>

#include "driscord_core.hpp"

static QVariantList parseDeviceJson(const std::string& json)
{
    QVariantList out;
    for (const auto& value : QJsonDocument::fromJson(
             QByteArray::fromStdString(json)).array()) {
        const auto object = value.toObject();
        out.push_back(QVariantMap {
            { QStringLiteral("id"), object["id"].toString() },
            { QStringLiteral("name"), object["name"].toString() },
        });
    }
    return out;
}

static constexpr auto kInputDeviceSetting = "audio/inputDevice";
static constexpr auto kOutputDeviceSetting = "audio/outputDevice";

static DriscordCore* g_core = nullptr;

DriscordBridge::DriscordBridge(
    QObject* parent, const QVector<IceServerSetting>& iceServers)
    : QObject(parent)
{
    std::vector<IceServer> coreIceServers;
    coreIceServers.reserve(static_cast<size_t>(iceServers.size()));
    for (const auto& server : iceServers) {
        coreIceServers.push_back(IceServer {
            .url = server.url.toStdString(),
            .username = server.username.toStdString(),
            .password = server.password.toStdString(),
        });
    }
    g_core = new DriscordCore(std::move(coreIceServers));

    g_core->transport.on_connected([this]() {
        QMetaObject::invokeMethod(this, [this] { emit wsConnected(); }, Qt::QueuedConnection);
    });
    g_core->transport.on_disconnected([this]() {
        QMetaObject::invokeMethod(this, [this] { emit wsDisconnected(); }, Qt::QueuedConnection);
    });

    g_core->set_on_peer_joined([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] { emit peerJoined(id); }, Qt::QueuedConnection);
    });
    g_core->set_on_peer_left([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] { emit peerLeft(id); }, Qt::QueuedConnection);
    });
    g_core->set_on_peer_identity([this](const std::string& id, const std::string& name) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id), name = QString::fromStdString(name)] { emit peerIdentityReceived(id, name); }, Qt::QueuedConnection);
    });
    g_core->set_on_streaming_started([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] { emit streamingStarted(id); }, Qt::QueuedConnection);
    });
    g_core->set_on_streaming_stopped([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] { emit streamingStopped(id); }, Qt::QueuedConnection);
    });
    g_core->set_on_watch_rejected(
        [this](const std::string& id, signaling::WatchRejectReason reason) {
            QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id), reason = QString::fromStdString(std::string(signaling::to_string(reason)))] { emit streamWatchRejected(id, reason); }, Qt::QueuedConnection);
        });
    g_core->set_on_frame([this](const std::string& id, const uint8_t* rgba, int w, int h) {
        QString qid = QString::fromStdString(id);
        if (m_frameProvider)
            m_frameProvider->updateFrame(qid, rgba, w, h);
        QMetaObject::invokeMethod(this, [this, qid] { emit frameUpdated(qid); }, Qt::QueuedConnection);
    });
    g_core->set_on_frame_removed([this](const std::string& id) {
        QString qid = QString::fromStdString(id);
        if (m_frameProvider)
            m_frameProvider->removeFrame(qid);
        QMetaObject::invokeMethod(this, [this, qid] { emit frameRemoved(qid); }, Qt::QueuedConnection);
    });
}

DriscordBridge::~DriscordBridge()
{
    delete g_core;
    g_core = nullptr;
}

void DriscordBridge::setFrameProvider(FrameProvider* fp) { m_frameProvider = fp; }
void DriscordBridge::setThumbnailProvider(ThumbnailProvider* tp) { m_thumbnailProvider = tp; }

QString DriscordBridge::grabThumbnail(const QString& targetJson, int maxW, int maxH)
{
    if (!m_thumbnailProvider)
        return { };
    auto result = g_core->capture_grab_thumbnail(targetJson.toStdString(), maxW, maxH);
    if (result.rgba.empty() || result.width <= 0 || result.height <= 0)
        return { };

    QImage img(result.rgba.data(), result.width, result.height, QImage::Format_RGBA8888);
    auto obj = QJsonDocument::fromJson(targetJson.toUtf8()).object();
    QString key = QString::number(obj["type"].toInt()) + ":" + obj["id"].toString();
    m_thumbnailProvider->store(key, img.copy());
    return "image://thumbs/" + key;
}

// -- Transport --

void DriscordBridge::connect(const QString& serverUrl, const QString& username)
{
    QString url = serverUrl;
    url += (url.contains('?') ? '&' : '?');
    url += "u=" + QString::fromUtf8(QUrl::toPercentEncoding(username));
    g_core->transport.connect(url.toStdString());
}

void DriscordBridge::disconnect() { g_core->transport.disconnect(); }
bool DriscordBridge::connected() const { return g_core->transport.connected(); }
QString DriscordBridge::localId() const { return QString::fromStdString(g_core->transport.local_id()); }
QString DriscordBridge::peersJson() const { return QString::fromStdString(g_core->peers_json()); }
QString DriscordBridge::transportStatsJson() const { return QString::fromStdString(g_core->transport.stats_json()); }

// -- Audio --

// Native ADM initialization may query the platform audio stack, so keep it
// off the Qt event loop.
void DriscordBridge::audioStart()
{
    QThreadPool::globalInstance()->start([] {
        QSettings settings;
        const auto input = settings.value(
            kInputDeviceSetting, QStringLiteral("default")).toString();
        const auto output = settings.value(
            kOutputDeviceSetting, QStringLiteral("default")).toString();
        if (!g_core->audio_set_input_device(input.toStdString())) {
            (void)g_core->audio_set_input_device("default");
            settings.setValue(kInputDeviceSetting, QStringLiteral("default"));
        }
        if (!g_core->audio_set_output_device(output.toStdString())) {
            (void)g_core->audio_set_output_device("default");
            settings.setValue(kOutputDeviceSetting, QStringLiteral("default"));
        }
        g_core->voice_start();
    });
}
void DriscordBridge::audioStop() { g_core->voice_stop(); }

void DriscordBridge::setMuted(bool m) { g_core->voice_set_muted(m); }
bool DriscordBridge::muted() const { return g_core->voice_muted(); }
void DriscordBridge::setDeafened(bool d) { g_core->audio_set_deafened(d); }
bool DriscordBridge::deafened() const { return g_core->audio_deafened(); }

void DriscordBridge::setMasterVolume(float v) { g_core->audio_set_master_volume(v); }
float DriscordBridge::masterVolume() const { return g_core->audio_master_volume(); }
float DriscordBridge::inputLevel() const { return g_core->audio_input_level(); }
float DriscordBridge::outputLevel() const { return g_core->audio_output_level(); }
void DriscordBridge::setNoiseGate(float t) { g_core->audio_set_noise_gate(t); }

QVariantList DriscordBridge::listInputDevices() const
{
    return parseDeviceJson(g_core->audio_input_devices_json());
}
QVariantList DriscordBridge::listOutputDevices() const
{
    return parseDeviceJson(g_core->audio_output_devices_json());
}
bool DriscordBridge::setInputDevice(const QString& id)
{
    if (!g_core->audio_set_input_device(id.toStdString()))
        return false;
    QSettings().setValue(kInputDeviceSetting, id);
    return true;
}
bool DriscordBridge::setOutputDevice(const QString& id)
{
    if (!g_core->audio_set_output_device(id.toStdString()))
        return false;
    QSettings().setValue(kOutputDeviceSetting, id);
    return true;
}
QString DriscordBridge::currentInputDevice() const
{
    return QSettings().value(
        kInputDeviceSetting, QStringLiteral("default")).toString();
}
QString DriscordBridge::currentOutputDevice() const
{
    return QSettings().value(
        kOutputDeviceSetting, QStringLiteral("default")).toString();
}

void DriscordBridge::setPeerVolume(const QString& id, float v) { g_core->audio_set_peer_volume(id.toStdString(), v); }
float DriscordBridge::peerVolume(const QString& id) const { return g_core->audio_peer_volume(id.toStdString()); }
void DriscordBridge::setPeerMuted(const QString& id, bool m) { g_core->audio_set_peer_muted(id.toStdString(), m); }
bool DriscordBridge::peerMuted(const QString& id) const { return g_core->audio_peer_muted(id.toStdString()); }

// -- Screen / Video --

void DriscordBridge::initScreenSession()
{
    g_core->init_screen_session();
}
void DriscordBridge::deinitScreenSession()
{
    g_core->deinit_screen_session();
}

QString DriscordBridge::captureVideoTargetsJson() const
{
    return QString::fromStdString(g_core->capture_video_list_targets_json());
}
QString DriscordBridge::captureAudioTargetsJson() const
{
    return QString::fromStdString(g_core->capture_audio_list_targets_json());
}

void DriscordBridge::startSharing(const QString& targetJson, int maxW, int maxH, int fps, bool audio)
{
    (void)g_core->screen_start_sharing(
        targetJson.toStdString(), maxW, maxH, fps, audio);
}
void DriscordBridge::stopSharing() { g_core->screen_stop_sharing(); }
bool DriscordBridge::sharing() const
{
    return g_core->screen_sharing();
}
void DriscordBridge::setLocalPreviewEnabled(bool enabled)
{
    g_core->screen_set_local_preview_enabled(enabled);
}

bool DriscordBridge::videoWatching() const { return g_core->video_watching(); }

void DriscordBridge::joinStream(const QString& id) { g_core->join_stream(id.toStdString()); }
void DriscordBridge::leaveStream(const QString& id)
{
    g_core->leave_stream(id.toStdString());
}

QString DriscordBridge::screenStatsJson(const QString& peerId) const
{
    return QString::fromStdString(
        g_core->screen_stats_json(peerId.toStdString()));
}

void DriscordBridge::setStreamVolume(const QString& id, float v)
{
    g_core->screen_set_stream_volume(id.toStdString(), v);
}
float DriscordBridge::streamVolume(const QString& id) const
{
    return g_core->screen_stream_volume(id.toStdString());
}
