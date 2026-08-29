#pragma once
#include <QString>

namespace install_swap {

struct SwapResult {
    bool ok = false;
    QString error;
};

SwapResult moveTreePerFile(const QString& fromDir, const QString& toDir);

SwapResult applyPayload(const QString& rootDir, const QString& payloadDir,
    const QString& backupDir);

SwapResult applyImageFile(const QString& imagePath,
    const QString& downloadedPath, const QString& backupDir);

void cleanupUpdateDir(const QString& rootDir);

}
