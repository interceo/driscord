#pragma once
#include <QString>
#include <QVector>

struct IceServerSetting {
    QString url;
    QString username;
    QString password;
};

struct AppConfig {
    int screenFps = 60;
    QVector<IceServerSetting> iceServers;
    QString updateUrl;
    QString updateChannel;
    QString updatePublicKey;

    static AppConfig load();
};
