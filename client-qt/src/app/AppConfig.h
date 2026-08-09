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
    QString server = "localhost:9001";
    QString api = "localhost:9002";
    int screenFps = 60;
    QVector<IceServerSetting> iceServers;

    QString signalingUrl() const;
    QString apiBaseUrl() const;

    static AppConfig load();
};
