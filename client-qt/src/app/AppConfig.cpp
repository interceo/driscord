#include "AppConfig.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static QString findConfigFile()
{
    // User settings belong to the platform config directory so they survive
    // replacing an unpacked application release.
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

    // A working-directory file remains useful for local development and
    // portable, explicitly user-managed installations.
    for (const auto* name : { "config.json", "driscord.json" }) {
        QString p = QDir::currentPath() + "/" + QLatin1String(name);
        if (QFile::exists(p)) {
            qDebug().noquote() << "[config] loaded from" << p;
            return p;
        }
    }

    qDebug().noquote() << "[config] no config file found, using defaults";
    return { };
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
