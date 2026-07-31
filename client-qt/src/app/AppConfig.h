#pragma once
#include <QList>
#include <QString>

struct TurnServerConfig {
    QString url, user, pass;
};

struct AppConfig {
    QString server = "localhost:9001";
    QString api = "localhost:9002";
    int screenFps = 60;
    QList<TurnServerConfig> turnServers;

    QString signalingUrl() const;
    QString apiBaseUrl() const;

    static AppConfig load();
};
