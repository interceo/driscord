#include "AppConfig.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <algorithm>

static QString findConfigFile()
{
    // 1. CWD — user-editable, survives app updates (mirrors Kotlin configCandidates)
    for (const auto* name : { "config.json", "driscord.json" }) {
        QString p = QDir::currentPath() + "/" + QLatin1String(name);
        if (QFile::exists(p)) {
            qDebug().noquote() << "[config] loaded from" << p;
            return p;
        }
    }
    // 2. Platform config dir (~/.config/driscord/ on Linux, %LOCALAPPDATA%\driscord\ on Windows)
#ifdef Q_OS_WIN
    QString appData = qEnvironmentVariable("LOCALAPPDATA");
    if (!appData.isEmpty()) {
        QString p = appData + "/driscord/config.json";
        if (QFile::exists(p)) {
            qDebug().noquote() << "[config] loaded from" << p;
            return p;
        }
    }
#else
    QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    QString base = xdg.isEmpty() ? QDir::homePath() + "/.config" : xdg;
    QString p = base + "/driscord/config.json";
    if (QFile::exists(p)) {
        qDebug().noquote() << "[config] loaded from" << p;
        return p;
    }
#endif
    qDebug().noquote() << "[config] no config file found, using defaults";
    return { };
}

static QString normalizedUrl(QString value, const QString& defaultScheme,
    const QStringList& acceptedSchemes)
{
    const bool hasAcceptedScheme = std::any_of(acceptedSchemes.cbegin(),
        acceptedSchemes.cend(), [&value](const QString& scheme) {
            return value.startsWith(scheme + "://", Qt::CaseInsensitive);
        });
    if (!hasAcceptedScheme)
        value.prepend(defaultScheme + "://");
    while (value.endsWith('/'))
        value.chop(1);
    return value;
}

QString AppConfig::signalingUrl() const
{
    return normalizedUrl(server, "ws", { "ws", "wss" });
}

QString AppConfig::apiBaseUrl() const
{
    return normalizedUrl(api, "http", { "http", "https" });
}

AppConfig AppConfig::load()
{
    AppConfig cfg;

    QString path = findConfigFile();
    if (path.isEmpty())
        return cfg;

    QFile f(path);
    if (!f.open(QFile::ReadOnly))
        return cfg;

    auto doc = QJsonDocument::fromJson(f.readAll());
    auto obj = doc.object();

    if (obj.contains("server"))
        cfg.server = obj["server"].toString();
    if (obj.contains("api"))
        cfg.api = obj["api"].toString();
    if (obj.contains("screen_fps")) {
        const int configuredFps = obj["screen_fps"].toInt(60);
        cfg.screenFps = configuredFps == 30 || configuredFps == 60
            ? configuredFps
            : 60;
    }
    // Accepted as either a bare URL string or an object carrying credentials.
    // "stun_servers" is the same list; TURN entries simply add credentials.
    QVector<IceServerSetting> configured;
    for (const auto* key : { "turn_servers", "stun_servers" }) {
        for (const auto value : obj[QLatin1String(key)].toArray()) {
            IceServerSetting server;
            if (value.isString()) {
                server.url = value.toString();
            } else {
                const auto entry = value.toObject();
                server.url = entry["url"].toString();
                server.username = entry["user"].toString();
                server.password = entry["pass"].toString();
            }
            if (server.url.isEmpty()) {
                qWarning().noquote() << "[config] ignoring" << key
                                     << "entry without a url";
                continue;
            }
            configured.append(server);
        }
    }
    // ICE infrastructure is explicit configuration. Without this list the
    // client uses host candidates only and contacts no third-party relay.
    if (!configured.isEmpty()) {
        cfg.iceServers = configured;
    }
    for (const auto& server : std::as_const(cfg.iceServers)) {
        qInfo().noquote() << "[config] ICE server" << server.url;
    }

    return cfg;
}
