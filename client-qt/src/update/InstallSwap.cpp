#include "InstallSwap.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <algorithm>

namespace install_swap {
namespace {

    const QString kUpdateDirName = QStringLiteral(".update");

    bool renameWithRetry(const QString& from, const QString& to)
    {
        for (int attempt = 0;; ++attempt) {
            if (QFile::rename(from, to)) {
                return true;
            }
            if (attempt >= 2) {
                return false;
            }
            QThread::msleep(100);
        }
    }

    SwapResult moveEntry(const QString& from, const QString& to)
    {
        if (QFileInfo(from).isDir() && !QFileInfo(from).isSymLink()) {
            if (!QDir().mkpath(to)) {
                return { false, QStringLiteral("cannot create %1").arg(to) };
            }
            const QDir fromDir(from);
            const auto entries = fromDir.entryList(
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                | QDir::System);
            for (const auto& name : entries) {
                const auto result
                    = moveEntry(fromDir.filePath(name), to + '/' + name);
                if (!result.ok) {
                    return result;
                }
            }
            QDir().rmdir(from);
            return { true, { } };
        }
        if (QFileInfo::exists(to)) {
            return { false, QStringLiteral("%1 already exists").arg(to) };
        }
        if (!renameWithRetry(from, to)) {
            return { false,
                QStringLiteral("cannot move %1 to %2").arg(from, to) };
        }
        return { true, { } };
    }

}

SwapResult moveTreePerFile(const QString& fromDir, const QString& toDir)
{
    if (!QFileInfo(fromDir).isDir()) {
        return { false, QStringLiteral("%1 is not a directory").arg(fromDir) };
    }
    return moveEntry(fromDir, toDir);
}

SwapResult applyPayload(const QString& rootDir, const QString& payloadDir,
    const QString& backupDir)
{
    const QDir payload(payloadDir);
    const QDir root(rootDir);
    if (!payload.exists()) {
        return { false, QStringLiteral("payload %1 is missing").arg(payloadDir) };
    }
    if (!QDir().mkpath(backupDir)) {
        return { false, QStringLiteral("cannot create %1").arg(backupDir) };
    }

    const auto names = payload.entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    QStringList backedUp;
    QStringList installed;

    const auto rollback = [&] {
        for (auto it = installed.crbegin(); it != installed.crend(); ++it) {
            moveEntry(root.filePath(*it), payloadDir + '/' + *it);
        }
        for (auto it = backedUp.crbegin(); it != backedUp.crend(); ++it) {
            moveEntry(backupDir + '/' + *it, root.filePath(*it));
        }
    };

    for (const auto& name : names) {
        if (QFileInfo::exists(root.filePath(name))) {
            const auto result
                = moveEntry(root.filePath(name), backupDir + '/' + name);
            if (!result.ok) {
                rollback();
                return { false,
                    QStringLiteral("backup of %1 failed: %2")
                        .arg(name, result.error) };
            }
            backedUp.append(name);
        }
        const auto result
            = moveEntry(payload.filePath(name), root.filePath(name));
        if (!result.ok) {
            rollback();
            return { false,
                QStringLiteral("install of %1 failed: %2")
                    .arg(name, result.error) };
        }
        installed.append(name);
    }
    return { true, { } };
}

SwapResult applyImageFile(const QString& imagePath,
    const QString& downloadedPath, const QString& backupDir)
{
    const QFileInfo image(imagePath);
    if (!image.isFile()) {
        return { false, QStringLiteral("%1 is not a file").arg(imagePath) };
    }
    if (!QFileInfo(downloadedPath).isFile()) {
        return { false,
            QStringLiteral("%1 is not a file").arg(downloadedPath) };
    }
    if (!QDir().mkpath(backupDir)) {
        return { false, QStringLiteral("cannot create %1").arg(backupDir) };
    }
    const QString backupPath = backupDir + '/' + image.fileName();
    QFile::remove(backupPath);
    if (!renameWithRetry(imagePath, backupPath)) {
        return { false,
            QStringLiteral("cannot move %1 aside").arg(imagePath) };
    }
    if (!renameWithRetry(downloadedPath, imagePath)) {
        renameWithRetry(backupPath, imagePath);
        return { false,
            QStringLiteral("cannot move %1 into place").arg(downloadedPath) };
    }
    QFile::setPermissions(imagePath,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
            | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther
            | QFile::ExeOther);
    return { true, { } };
}

void cleanupUpdateDir(const QString& rootDir)
{
    const QString updateDir = QDir(rootDir).filePath(kUpdateDirName);
    if (!QFileInfo(updateDir).isDir()) {
        return;
    }
    QDirIterator it(updateDir, QDir::Files | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile::remove(it.next());
    }
    QDirIterator dirs(updateDir, QDir::Dirs | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    QStringList paths;
    while (dirs.hasNext()) {
        paths.append(dirs.next());
    }
    std::sort(paths.begin(), paths.end(),
        [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const auto& path : paths) {
        QDir().rmdir(path);
    }
    QDir().rmdir(updateDir);
}

}
