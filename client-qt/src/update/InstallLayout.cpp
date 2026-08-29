#include "InstallLayout.h"

#include <QDir>
#include <QFileInfo>

InstallLayout detectInstallLayout(const QString& exePath,
    const QString& appImagePath)
{
    if (!appImagePath.isEmpty()) {
        const QFileInfo image(appImagePath);
        if (image.isAbsolute() && image.isFile()) {
            return { InstallLayout::Kind::AppImage, true,
                image.absolutePath(), image.absoluteFilePath(),
                image.absoluteFilePath() };
        }
        return { };
    }

#ifdef Q_OS_WIN
    const QFileInfo exe(exePath);
    const QDir exeDir = exe.absoluteDir();
    if (exe.fileName() == QLatin1String("driscord_client.exe")
        && exeDir.exists(QStringLiteral("qt.conf"))
        && QFileInfo(exeDir.filePath(QStringLiteral("plugins"))).isDir()) {
        return { InstallLayout::Kind::WindowsFlat, true,
            exeDir.absolutePath(), exe.absoluteFilePath(), { } };
    }
#else
    Q_UNUSED(exePath);
#endif
    return { };
}
