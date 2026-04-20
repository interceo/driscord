#include "DriscordBridge.h"
#include <QMetaObject>
#include <QThread>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "driscord_core.hpp"

static QStringList parseDeviceJson(const std::string& json) {
    QStringList out;
    for (const auto& v : QJsonDocument::fromJson(QByteArray::fromStdString(json)).array())
        out << v.toObject()["name"].toString();
    return out;
}

static DriscordCore* g_core = nullptr;

DriscordBridge::DriscordBridge(QObject* parent) : QObject(parent) {
    g_core = new DriscordCore();

    // ~60Hz tick — drives ScreenSession::update() which decodes queued chunks
    // and invokes on_frame_cb_. Started/stopped with the screen session.
    m_screenTickTimer = new QTimer(this);
    m_screenTickTimer->setInterval(16);
    QObject::connect(m_screenTickTimer, &QTimer::timeout, this, [] {
        if (g_core && g_core->screen_session.has_value()) {
            g_core->screen_session->update();
        }
    });

    g_core->transport.on_connected([this]() {
        QMetaObject::invokeMethod(this, [this] { emit wsConnected(); }, Qt::QueuedConnection);
    });
    g_core->transport.on_disconnected([this]() {
        QMetaObject::invokeMethod(this, [this] { emit wsDisconnected(); }, Qt::QueuedConnection);
    });

    g_core->set_on_peer_joined([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] {
            emit peerJoined(id);
        }, Qt::QueuedConnection);
    });
    g_core->set_on_peer_left([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] {
            emit peerLeft(id);
        }, Qt::QueuedConnection);
    });
    g_core->set_on_peer_identity([this](const std::string& id, const std::string& name) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id), name = QString::fromStdString(name)] {
            emit peerIdentityReceived(id, name);
        }, Qt::QueuedConnection);
    });
    g_core->set_on_streaming_started([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] {
            emit streamingStarted(id);
        }, Qt::QueuedConnection);
    });
    g_core->set_on_streaming_stopped([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] {
            emit streamingStopped(id);
        }, Qt::QueuedConnection);
    });
    g_core->set_on_new_streaming_peer([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] {
            emit newStreamingPeer(id);
        }, Qt::QueuedConnection);
    });
    g_core->set_on_streaming_peer_removed([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] {
            emit streamingPeerRemoved(id);
        }, Qt::QueuedConnection);
    });
    g_core->set_on_frame([this](const std::string& id, const uint8_t* rgba, int w, int h) {
        QString qid = QString::fromStdString(id);
        if (m_frameProvider) m_frameProvider->updateFrame(qid, rgba, w, h);
        QMetaObject::invokeMethod(this, [this, qid] {
            emit frameUpdated(qid);
        }, Qt::QueuedConnection);
    });
    g_core->set_on_frame_removed([this](const std::string& id) {
        QString qid = QString::fromStdString(id);
        if (m_frameProvider) m_frameProvider->removeFrame(qid);
        QMetaObject::invokeMethod(this, [this, qid] {
            emit frameRemoved(qid);
        }, Qt::QueuedConnection);
    });
}

DriscordBridge::~DriscordBridge() {
    delete g_core;
    g_core = nullptr;
}

void DriscordBridge::setFrameProvider(FrameProvider* fp) { m_frameProvider = fp; }

// -- Transport --

void DriscordBridge::addTurnServer(const QString& url, const QString& user, const QString& pass) {
    g_core->transport.add_turn_server(url.toStdString(), user.toStdString(), pass.toStdString());
}

void DriscordBridge::connect(const QString& serverUrl, const QString& username) {
    g_core->set_local_username(username.toStdString());
    g_core->transport.connect(serverUrl.toStdString());
}

void DriscordBridge::disconnect() { g_core->transport.disconnect(); }
bool DriscordBridge::connected() const { return g_core->transport.connected(); }
QString DriscordBridge::localId() const { return QString::fromStdString(g_core->transport.local_id()); }
QString DriscordBridge::peersJson() const { return QString::fromStdString(g_core->peers_json()); }
QString DriscordBridge::transportStatsJson() const { return QString::fromStdString(g_core->transport.stats_json()); }

// -- Audio --

void DriscordBridge::audioStart() { g_core->audio_transport.start(); }
void DriscordBridge::audioStop()  { g_core->audio_transport.stop(); }

void DriscordBridge::setMuted(bool m)    { g_core->audio_transport.set_self_muted(m); }
bool DriscordBridge::muted() const       { return g_core->audio_transport.self_muted(); }
void DriscordBridge::setDeafened(bool d) { g_core->audio_transport.set_deafened(d); }
bool DriscordBridge::deafened() const    { return g_core->audio_transport.deafened(); }

void DriscordBridge::setMasterVolume(float v) { g_core->audio_transport.set_master_volume(v); }
float DriscordBridge::masterVolume() const    { return g_core->audio_transport.master_volume(); }
float DriscordBridge::inputLevel() const      { return g_core->audio_transport.input_level(); }
float DriscordBridge::outputLevel() const     { return g_core->audio_transport.output_level(); }
void DriscordBridge::setNoiseGate(float t)    { g_core->audio_transport.set_noise_gate(t); }

QStringList DriscordBridge::listInputDevices() const {
    return parseDeviceJson(g_core->audio_transport.list_input_devices_json());
}
QStringList DriscordBridge::listOutputDevices() const {
    return parseDeviceJson(g_core->audio_transport.list_output_devices_json());
}
void DriscordBridge::setInputDevice(const QString& id)  { g_core->audio_transport.set_input_device(id.toStdString()); }
void DriscordBridge::setOutputDevice(const QString& id) { g_core->audio_transport.set_output_device(id.toStdString()); }

void DriscordBridge::setPeerVolume(const QString& id, float v) { g_core->audio_transport.set_peer_volume(id.toStdString(), v); }
float DriscordBridge::peerVolume(const QString& id) const      { return g_core->audio_transport.peer_volume(id.toStdString()); }
void DriscordBridge::setPeerMuted(const QString& id, bool m)   { g_core->audio_transport.set_peer_muted(id.toStdString(), m); }
bool DriscordBridge::peerMuted(const QString& id) const        { return g_core->audio_transport.peer_muted(id.toStdString()); }

// -- Screen / Video --

void DriscordBridge::initScreenSession()   {
    g_core->init_screen_session();
    m_screenTickTimer->start();
}
void DriscordBridge::deinitScreenSession() {
    m_screenTickTimer->stop();
    g_core->deinit_screen_session();
}

QString DriscordBridge::captureVideoTargetsJson() const {
    return QString::fromStdString(g_core->capture_video_list_targets_json());
}
QString DriscordBridge::captureAudioTargetsJson() const {
    return QString::fromStdString(g_core->capture_audio_list_targets_json());
}

void DriscordBridge::startSharing(const QString& targetJson, int maxW, int maxH, int fps, bool audio) {
    g_core->screen_start_sharing(targetJson.toStdString(), maxW, maxH, fps, audio);
}
void DriscordBridge::stopSharing()    { g_core->screen_stop_sharing(); }
bool DriscordBridge::sharing() const  {
    return g_core->screen_session.has_value() && g_core->screen_session->sharing();
}

void DriscordBridge::setVideoWatching(bool w) { g_core->video_set_watching(w); }
bool DriscordBridge::videoWatching() const    { return g_core->video_transport.watching(); }

void DriscordBridge::joinStream(const QString& id)  { g_core->join_stream(id.toStdString()); }
void DriscordBridge::leaveStream()                  { g_core->leave_stream(); }

QString DriscordBridge::screenStatsJson() const {
    if (!g_core->screen_session.has_value()) return "{}";
    return QString::fromStdString(g_core->screen_session->stats_json());
}

void DriscordBridge::setStreamVolume(const QString& id, float v) {
    g_core->screen_set_stream_volume(id.toStdString(), v);
}
float DriscordBridge::streamVolume() const { return g_core->screen_stream_volume(); }
