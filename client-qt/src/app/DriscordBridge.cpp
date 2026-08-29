#include "DriscordBridge.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSettings>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include "driscord_core.hpp"

static QVariantList parseDeviceJson(const std::string& json)
{
    QVariantList out;
    for (const auto& value : QJsonDocument::fromJson(
             QByteArray::fromStdString(json))
             .array()) {
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

DriscordBridge::DriscordBridge(
    QObject* parent, const QVector<IceServerSetting>& iceServers)
    : QObject(parent)
{
    m_audioPool.setMaxThreadCount(1);
    m_thumbnailPool.setMaxThreadCount(1);

    std::vector<IceServer> coreIceServers;
    coreIceServers.reserve(static_cast<size_t>(iceServers.size()));
    for (const auto& server : iceServers) {
        coreIceServers.push_back(IceServer {
            .url = server.url.toStdString(),
            .username = server.username.toStdString(),
            .password = server.password.toStdString(),
        });
    }
    m_core = std::make_unique<DriscordCore>(std::move(coreIceServers));

    m_core->transport.on_connected([this]() {
        QMetaObject::invokeMethod(this, [this] { emit wsConnected(); }, Qt::QueuedConnection);
    });
    m_core->transport.on_disconnected([this]() {
        QMetaObject::invokeMethod(this, [this] { emit wsDisconnected(); }, Qt::QueuedConnection);
    });

    m_core->set_on_peer_joined([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] { emit peerJoined(id); }, Qt::QueuedConnection);
    });
    m_core->set_on_peer_left([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] { emit peerLeft(id); }, Qt::QueuedConnection);
    });
    m_core->set_on_peer_identity([this](const std::string& id, const std::string& name) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id), name = QString::fromStdString(name)] { emit peerIdentityReceived(id, name); }, Qt::QueuedConnection);
    });
    m_core->set_on_streaming_started([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] { emit streamingStarted(id); }, Qt::QueuedConnection);
    });
    m_core->set_on_streaming_stopped([this](const std::string& id) {
        QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id)] { emit streamingStopped(id); }, Qt::QueuedConnection);
    });
    m_core->set_on_watch_rejected(
        [this](const std::string& id, signaling::WatchRejectReason reason) {
            QMetaObject::invokeMethod(this, [this, id = QString::fromStdString(id), reason = QString::fromStdString(std::string(signaling::to_string(reason)))] { emit streamWatchRejected(id, reason); }, Qt::QueuedConnection);
        });
    m_core->set_on_frame([this](const std::string& id,
                             const DriscordCore::VideoFrameView& f) {
        QMutexLocker lock(&m_sinkMutex);
        const QList<QVideoSink*> sinks
            = m_videoSinks.value(QString::fromStdString(id));
        if (sinks.isEmpty())
            return;
        QVideoFrame frame(QVideoFrameFormat(
            QSize(f.width, f.height), QVideoFrameFormat::Format_YUV420P));
        if (!frame.map(QVideoFrame::WriteOnly))
            return;
        const struct {
            const uint8_t* data;
            int stride;
            int rows;
            int row_bytes;
        } planes[3] = {
            { f.y, f.y_stride, f.height, f.width },
            { f.u, f.u_stride, f.chroma_height(), f.chroma_width() },
            { f.v, f.v_stride, f.chroma_height(), f.chroma_width() },
        };
        for (int plane = 0; plane < 3; ++plane) {
            uchar* dst = frame.bits(plane);
            const int dstStride = frame.bytesPerLine(plane);
            for (int row = 0; row < planes[plane].rows; ++row) {
                memcpy(dst + static_cast<size_t>(row) * dstStride,
                    planes[plane].data
                        + static_cast<size_t>(row) * planes[plane].stride,
                    planes[plane].row_bytes);
            }
        }
        frame.unmap();
        for (QVideoSink* sink : sinks)
            sink->setVideoFrame(frame);
    });
    m_core->set_on_frame_removed([this](const std::string& id) {
        QString qid = QString::fromStdString(id);
        {
            QMutexLocker lock(&m_sinkMutex);
            for (QVideoSink* sink : m_videoSinks.value(qid))
                sink->setVideoFrame(QVideoFrame());
        }
        QMetaObject::invokeMethod(this, [this, qid] { emit frameRemoved(qid); }, Qt::QueuedConnection);
    });
}

DriscordBridge::~DriscordBridge()
{
    shutdown();
}

void DriscordBridge::shutdown()
{
    if (!m_core)
        return;
    ++m_audioGeneration;
    m_audioPool.waitForDone();
    m_thumbnailPool.waitForDone();
    m_core->deinit_screen_session();
    m_core->voice_stop();
    m_core->transport.disconnect();
    {
        QMutexLocker lock(&m_sinkMutex);
        m_videoSinks.clear();
    }
    m_thumbnailProvider = nullptr;
    m_core.reset();
}

void DriscordBridge::setThumbnailProvider(ThumbnailProvider* tp) { m_thumbnailProvider = tp; }

void DriscordBridge::registerVideoSink(const QString& peerId, QVideoSink* sink)
{
    if (peerId.isEmpty() || sink == nullptr)
        return;
    {
        QMutexLocker lock(&m_sinkMutex);
        auto& sinks = m_videoSinks[peerId];
        if (!sinks.contains(sink))
            sinks.append(sink);
    }
    QObject::connect(sink, &QObject::destroyed, this, [this, peerId, sink] {
        QMutexLocker lock(&m_sinkMutex);
        const auto found = m_videoSinks.find(peerId);
        if (found != m_videoSinks.end()) {
            found->removeAll(sink);
            if (found->isEmpty())
                m_videoSinks.erase(found);
        }
    });
}

void DriscordBridge::unregisterVideoSink(const QString& peerId, QVideoSink* sink)
{
    QMutexLocker lock(&m_sinkMutex);
    const auto found = m_videoSinks.find(peerId);
    if (found != m_videoSinks.end()) {
        found->removeAll(sink);
        if (found->isEmpty())
            m_videoSinks.erase(found);
    }
}

void DriscordBridge::requestThumbnail(const QString& targetJson, int maxW, int maxH)
{
    if (!m_thumbnailProvider) {
        emit thumbnailReady(targetJson, { });
        return;
    }
    m_thumbnailPool.start([this, targetJson, maxW, maxH] {
        QString url;
        if (m_core) {
            auto result = m_core->capture_grab_thumbnail(
                targetJson.toStdString(), maxW, maxH);
            if (!result.rgba.empty() && result.width > 0 && result.height > 0) {
                QImage img(result.rgba.data(), result.width, result.height,
                    QImage::Format_RGBA8888);
                const auto obj
                    = QJsonDocument::fromJson(targetJson.toUtf8()).object();
                const QString key = QString::number(obj["type"].toInt()) + ":"
                    + obj["id"].toString();
                if (m_thumbnailProvider) {
                    m_thumbnailProvider->store(key, img.copy());
                    url = "image://thumbs/" + key;
                }
            }
        }
        QMetaObject::invokeMethod(
            this, [this, targetJson, url] { emit thumbnailReady(targetJson, url); },
            Qt::QueuedConnection);
    });
}

void DriscordBridge::connect(const QString& serverUrl,
    const QString& username,
    const QString& accessToken)
{
    QString url = serverUrl;
    url += (url.contains('?') ? '&' : '?');
    url += "u=" + QString::fromUtf8(QUrl::toPercentEncoding(username));
    if (!accessToken.isEmpty()) {
        url += "&t=" + QString::fromUtf8(QUrl::toPercentEncoding(accessToken));
    }
    m_core->transport.connect(url.toStdString());
}

void DriscordBridge::disconnect() { m_core->transport.disconnect(); }
bool DriscordBridge::connected() const { return m_core->transport.connected(); }
QString DriscordBridge::localId() const { return QString::fromStdString(m_core->transport.local_id()); }
QString DriscordBridge::voiceStatsJson() const
{
    return QString::fromStdString(m_core->voice_stats_json());
}

void DriscordBridge::audioStart()
{
    const auto generation = ++m_audioGeneration;
    m_audioPool.start([this, generation] {
        QSettings settings;
        const auto input = settings.value(
                                       kInputDeviceSetting, QStringLiteral("default"))
                               .toString();
        const auto output = settings.value(
                                        kOutputDeviceSetting, QStringLiteral("default"))
                                .toString();
        if (!m_core->audio_set_input_device(input.toStdString())) {
            (void)m_core->audio_set_input_device("default");
            settings.setValue(kInputDeviceSetting, QStringLiteral("default"));
        }
        if (!m_core->audio_set_output_device(output.toStdString())) {
            (void)m_core->audio_set_output_device("default");
            settings.setValue(kOutputDeviceSetting, QStringLiteral("default"));
        }
        if (m_audioGeneration.load() == generation) {
            m_core->voice_start();
        }
    });
}
void DriscordBridge::audioStop()
{
    ++m_audioGeneration;
    m_audioPool.start([this] { m_core->voice_stop(); });
}

void DriscordBridge::setMuted(bool m) { m_core->voice_set_muted(m); }
bool DriscordBridge::muted() const { return m_core->voice_muted(); }
void DriscordBridge::setDeafened(bool d) { m_core->audio_set_deafened(d); }
bool DriscordBridge::deafened() const { return m_core->audio_deafened(); }

void DriscordBridge::setMasterVolume(float v) { m_core->audio_set_master_volume(v); }
float DriscordBridge::masterVolume() const { return m_core->audio_master_volume(); }

QVariantList DriscordBridge::listInputDevices() const
{
    return parseDeviceJson(m_core->audio_input_devices_json());
}
QVariantList DriscordBridge::listOutputDevices() const
{
    return parseDeviceJson(m_core->audio_output_devices_json());
}
bool DriscordBridge::setInputDevice(const QString& id)
{
    if (!m_core->audio_set_input_device(id.toStdString()))
        return false;
    QSettings().setValue(kInputDeviceSetting, id);
    return true;
}
bool DriscordBridge::setOutputDevice(const QString& id)
{
    if (!m_core->audio_set_output_device(id.toStdString()))
        return false;
    QSettings().setValue(kOutputDeviceSetting, id);
    return true;
}
QString DriscordBridge::currentInputDevice() const
{
    return QSettings().value(
                          kInputDeviceSetting, QStringLiteral("default"))
        .toString();
}
QString DriscordBridge::currentOutputDevice() const
{
    return QSettings().value(
                          kOutputDeviceSetting, QStringLiteral("default"))
        .toString();
}

void DriscordBridge::setPeerVolume(const QString& id, float v) { m_core->audio_set_peer_volume(id.toStdString(), v); }
float DriscordBridge::peerVolume(const QString& id) const { return m_core->audio_peer_volume(id.toStdString()); }
void DriscordBridge::setPeerMuted(const QString& id, bool m) { m_core->audio_set_peer_muted(id.toStdString(), m); }
bool DriscordBridge::peerMuted(const QString& id) const { return m_core->audio_peer_muted(id.toStdString()); }

void DriscordBridge::initScreenSession()
{
    m_core->init_screen_session();
}
void DriscordBridge::deinitScreenSession()
{
    m_core->deinit_screen_session();
}

QString DriscordBridge::captureVideoTargetsJson() const
{
    return QString::fromStdString(m_core->capture_video_list_targets_json());
}
QString DriscordBridge::captureAudioTargetsJson() const
{
    return QString::fromStdString(m_core->capture_audio_list_targets_json());
}

bool DriscordBridge::startSharing(const QString& targetJson,
    int maxW,
    int maxH,
    int fps,
    bool audio,
    const QString& audioTarget)
{
    return m_core->screen_start_sharing(targetJson.toStdString(), maxW, maxH,
        fps, audio, audioTarget.toStdString());
}
void DriscordBridge::stopSharing() { m_core->screen_stop_sharing(); }
bool DriscordBridge::sharing() const
{
    return m_core->screen_sharing();
}
void DriscordBridge::setLocalPreviewEnabled(bool enabled)
{
    m_core->screen_set_local_preview_enabled(enabled);
}

void DriscordBridge::joinStream(const QString& id) { m_core->join_stream(id.toStdString()); }
void DriscordBridge::leaveStream(const QString& id)
{
    m_core->leave_stream(id.toStdString());
}

QString DriscordBridge::screenStatsJson(const QString& peerId) const
{
    return QString::fromStdString(
        m_core->screen_stats_json(peerId.toStdString()));
}

void DriscordBridge::setStreamVolume(const QString& id, float v)
{
    m_core->screen_set_stream_volume(id.toStdString(), v);
}
