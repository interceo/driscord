#pragma once
#include <QString>

struct InstallLayout {
    enum class Kind { None,
        WindowsFlat,
        AppImage };

    Kind kind = Kind::None;
    bool portable = false;
    QString rootDir;
    QString relaunchPath;
    QString appImagePath;
};

InstallLayout detectInstallLayout(const QString& exePath,
    const QString& appImagePath = { });
