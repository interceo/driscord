#pragma once
#include <QString>
#include <QList>

struct TurnServerConfig {
    QString url, user, pass;
};

struct AppConfig {
    QString server   = "localhost:9001";
    QString api      = "localhost:9002";
    int     screenFps= 60;
    QList<TurnServerConfig> turnServers;

    QString signalingUrl() const { return "ws://"  + server; }
    QString apiBaseUrl()   const { return "http://" + api;   }

    static AppConfig load();
};
