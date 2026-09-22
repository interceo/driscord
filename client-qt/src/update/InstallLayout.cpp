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

#if defined(Q_OS_WIN)
    const QFileInfo exe(exePath);
    const QDir exeDir = exe.absoluteDir();
    if (exe.fileName() == QLatin1String("driscord_client.exe")
        && exeDir.exists(QStringLiteral("qt.conf"))
        && QFileInfo(exeDir.filePath(QStringLiteral("plugins"))).isDir()) {
        return { InstallLayout::Kind::WindowsFlat, true,
            exeDir.absolutePath(), exe.absoluteFilePath(), { } };
    }
#elif defined(Q_OS_MACOS)
    // .../Driscord.app/Contents/MacOS/driscord_client — the root is the
    // directory holding the bundle, never the bundle itself: staging an update
    // inside .app would invalidate its signature.
    const QFileInfo exe(exePath);
    QDir dir = exe.absoluteDir();
    if (dir.dirName() == QLatin1String("MacOS") && dir.cdUp()
        && dir.dirName() == QLatin1String("Contents") && dir.cdUp()
        && dir.dirName().endsWith(QLatin1String(".app")) && dir.cdUp()) {
        return { InstallLayout::Kind::MacBundle, true, dir.absolutePath(),
            exe.absoluteFilePath(), { } };
    }
#else
    Q_UNUSED(exePath);
#endif
    return { };
}
