#pragma once
#include <QString>
#include <QVector>

// A STUN or TURN server from config.json. Without at least one of these the
// client offers host candidates only, so a peer behind NAT is unreachable.
struct IceServerSetting {
    QString url;
    QString username;
    QString password;
};

struct AppConfig {
    int screenFps = 60;
    QVector<IceServerSetting> iceServers;

    static AppConfig load();
};
