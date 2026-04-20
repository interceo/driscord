#include "AppConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDebug>

static QString findConfigFile() {
    // 1. CWD — user-editable, survives app updates (mirrors Kotlin configCandidates)
    for (const QString& name : {"config.json", "driscord.json"}) {
        QString p = QDir::currentPath() + "/" + name;
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
    return {};
}

AppConfig AppConfig::load() {
    AppConfig cfg;
    QString path = findConfigFile();
    if (path.isEmpty()) return cfg;

    QFile f(path);
    if (!f.open(QFile::ReadOnly)) return cfg;

    auto doc = QJsonDocument::fromJson(f.readAll());
    auto obj = doc.object();

    if (obj.contains("server")) cfg.server    = obj["server"].toString();
    if (obj.contains("api"))    cfg.api        = obj["api"].toString();
    if (obj.contains("screen_fps")) cfg.screenFps = obj["screen_fps"].toInt(60);

    for (const auto& v : obj["turn_servers"].toArray()) {
        auto t = v.toObject();
        cfg.turnServers.append({t["url"].toString(), t["user"].toString(), t["pass"].toString()});
    }
    return cfg;
}
