#pragma once
#include <QString>

struct AppConfig {
    QString server = "localhost:9001";
    QString api = "localhost:9002";
    int screenFps = 60;

    QString signalingUrl() const;
    QString apiBaseUrl() const;

    static AppConfig load();
};
