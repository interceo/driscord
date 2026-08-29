#include "UpdateManager.h"

#include "InstallSwap.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

namespace {

constexpr qint64 kMaxManifestBytes = 64 * 1024;
constexpr qint64 kMaxSignatureBytes = 4 * 1024;
constexpr int kSmallRequestTimeoutMs = 15000;
constexpr int kDownloadInactivityMs = 60000;
constexpr int kStartupCleanupDelayMs = 3000;
constexpr int kStartupCheckDelayMs = 15000;
constexpr int kPeriodicCheckIntervalMs = 6 * 60 * 60 * 1000;

const QString kDismissedVersionKey = QStringLiteral("updates/dismissedVersion");

}

UpdateManager::UpdateManager(UpdateManagerConfig config, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    m_currentVersion
        = VersionTriple::parse(m_config.currentVersionCore).value_or(VersionTriple { });
    m_state = m_config.baseUrl.isEmpty() || m_config.trustedKeys.isEmpty()
        ? QStringLiteral("disabled")
        : QStringLiteral("idle");

    m_periodicTimer.setInterval(kPeriodicCheckIntervalMs);
    connect(&m_periodicTimer, &QTimer::timeout, this, [this] {
        if (m_state == QLatin1String("idle")
            || m_state == QLatin1String("upToDate")
            || m_state == QLatin1String("error")) {
            checkForUpdates();
        }
    });

    m_downloadWatchdog.setInterval(kDownloadInactivityMs);
    m_downloadWatchdog.setSingleShot(true);
    connect(&m_downloadWatchdog, &QTimer::timeout, this, [this] {
        if (m_activeReply && m_state == QLatin1String("downloading")) {
            m_activeReply->abort();
        }
    });
}

void UpdateManager::startBackgroundTasks()
{
    if (m_config.layout.portable) {
        QTimer::singleShot(kStartupCleanupDelayMs, this, [root = m_config.layout.rootDir] { install_swap::cleanupUpdateDir(root); });
    }
    if (m_state == QLatin1String("disabled")
        || !m_config.allowAutomaticChecks) {
        return;
    }
    QTimer::singleShot(kStartupCheckDelayMs, this, [this] {
        if (m_state == QLatin1String("idle")) {
            checkForUpdates();
        }
    });
    m_periodicTimer.start();
}

void UpdateManager::setState(const QString& state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void UpdateManager::fail(const QString& error)
{
    qWarning().noquote() << "[update]" << error;
    m_errorText = error;
    m_state = QStringLiteral("error");
    emit stateChanged();
}

QUrl UpdateManager::manifestUrl() const
{
    return QUrl(m_config.baseUrl + '/' + m_config.channel + '/'
        + m_config.pathTarget + QStringLiteral("/latest.json"));
}

QNetworkReply* UpdateManager::fetchSmall(const QUrl& url, qint64 maxBytes,
    std::function<void(QByteArray)> onSuccess)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(kSmallRequestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    auto* reply = m_nam.get(request);
    reply->setParent(this);
    const auto generation = m_generation;
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, maxBytes, generation,
            onSuccess = std::move(onSuccess)] {
            reply->deleteLater();
            if (generation != m_generation) {
                return;
            }
            m_activeReply = nullptr;
            const int status
                = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                      .toInt();
            if (reply->error() != QNetworkReply::NoError || status != 200) {
                fail(QStringLiteral("update check failed: %1")
                        .arg(status ? QString::number(status)
                                    : reply->errorString()));
                return;
            }
            if (reply->bytesAvailable() > maxBytes) {
                fail(QStringLiteral("update check failed: oversized response"));
                return;
            }
            onSuccess(reply->readAll());
        });
    m_activeReply = reply;
    return reply;
}

void UpdateManager::checkForUpdates()
{
    if (m_state == QLatin1String("disabled")
        || m_state == QLatin1String("checking")
        || m_state == QLatin1String("downloading")
        || m_state == QLatin1String("verifying")
        || m_state == QLatin1String("extracting")
        || m_state == QLatin1String("applying")) {
        return;
    }
    ++m_generation;
    m_errorText.clear();
    m_manifest.reset();
    m_latestVersion.clear();
    m_payloadPath.clear();
    m_canApply = false;
    m_applyHint.clear();
    setState(QStringLiteral("checking"));
    fetchSmall(manifestUrl(), kMaxManifestBytes,
        [this](QByteArray manifestBytes) {
            fetchSignatureAndVerify(manifestBytes);
        });
}

void UpdateManager::fetchSignatureAndVerify(const QByteArray& manifestBytes)
{
    QUrl signatureUrl = manifestUrl();
    signatureUrl.setPath(signatureUrl.path() + QStringLiteral(".minisig"));
    fetchSmall(signatureUrl, kMaxSignatureBytes,
        [this, manifestBytes](QByteArray signatureBytes) {
            const auto verdict = minisign::verifyDetached(
                manifestBytes, signatureBytes, m_config.trustedKeys);
            if (!verdict.ok) {
                fail(QStringLiteral("manifest signature rejected: %1")
                        .arg(verdict.error));
                return;
            }
            onManifestVerified(manifestBytes);
        });
}

void UpdateManager::onManifestVerified(const QByteArray& manifestBytes)
{
    auto parsed = parseUpdateManifest(manifestBytes, m_config.channel,
        m_config.manifestTarget, m_config.archiveSuffix);
    if (!parsed.manifest) {
        fail(QStringLiteral("manifest rejected: %1").arg(parsed.error));
        return;
    }
    m_manifest = std::move(parsed.manifest);
    m_latestVersion = m_manifest->version.toString();

    if (!(m_manifest->version > m_currentVersion)) {
        setState(QStringLiteral("upToDate"));
        return;
    }
    const auto dismissed
        = QSettings().value(kDismissedVersionKey).toString();
    const bool notice = m_latestVersion != dismissed;
    if (notice != m_noticeVisible) {
        m_noticeVisible = notice;
        emit noticeVisibleChanged();
    }
    setState(QStringLiteral("updateAvailable"));
}

QString UpdateManager::stagingDir() const
{
    if (stagingInsideRoot()) {
        return m_config.layout.rootDir + QStringLiteral("/.update");
    }
    return m_config.fallbackStagingDir;
}

bool UpdateManager::stagingInsideRoot() const
{
    return m_config.layout.portable
        && QFileInfo(m_config.layout.rootDir).isWritable();
}

void UpdateManager::downloadUpdate()
{
    if (m_state != QLatin1String("updateAvailable") || !m_manifest) {
        return;
    }
    const QUrl artifactUrl
        = manifestUrl().resolved(QUrl(m_manifest->file.url));
    const QUrl base(m_config.baseUrl);
    if (artifactUrl.scheme() != base.scheme()
        || artifactUrl.host() != base.host()
        || artifactUrl.port() != base.port()) {
        fail(QStringLiteral("artifact url leaves the update origin"));
        return;
    }

    const QString staging = stagingDir() + QStringLiteral("/download");
    if (!QDir().mkpath(staging)) {
        fail(QStringLiteral("cannot create %1").arg(staging));
        return;
    }
    const QStorageInfo storage(staging);
    if (storage.isValid()
        && storage.bytesAvailable() < 2 * m_manifest->file.size) {
        fail(QStringLiteral("not enough disk space in %1").arg(staging));
        return;
    }

    m_downloadFile.setFileName(
        staging + '/' + m_manifest->file.name + QStringLiteral(".part"));
    if (!m_downloadFile.open(QFile::WriteOnly | QFile::Truncate)) {
        fail(QStringLiteral("cannot write %1").arg(m_downloadFile.fileName()));
        return;
    }
    m_downloadHash.reset();
    m_downloadedBytes = 0;
    m_downloadProgress = 0.0;
    emit downloadProgressChanged();
    setState(QStringLiteral("downloading"));
    beginDownload();
}

void UpdateManager::beginDownload()
{
    ++m_generation;
    const auto generation = m_generation;
    QNetworkRequest request(
        manifestUrl().resolved(QUrl(m_manifest->file.url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    auto* reply = m_nam.get(request);
    reply->setParent(this);
    m_activeReply = reply;
    m_downloadWatchdog.start();

    connect(reply, &QNetworkReply::readyRead, this,
        [this, reply, generation] {
            if (generation != m_generation) {
                return;
            }
            m_downloadWatchdog.start();
            const QByteArray chunk = reply->readAll();
            m_downloadHash.addData(chunk);
            m_downloadedBytes += chunk.size();
            if (m_downloadedBytes > m_manifest->file.size) {
                reply->abort();
                return;
            }
            if (m_downloadFile.write(chunk) != chunk.size()) {
                reply->abort();
                return;
            }
            const double progress
                = double(m_downloadedBytes) / double(m_manifest->file.size);
            if (progress - m_downloadProgress >= 0.01) {
                m_downloadProgress = progress;
                emit downloadProgressChanged();
            }
        });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation) {
            return;
        }
        m_activeReply = nullptr;
        m_downloadWatchdog.stop();
        onDownloadFinished(reply);
    });
}

void UpdateManager::onDownloadFinished(QNetworkReply* reply)
{
    const int status
        = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_downloadFile.close();
    const auto discard = [this] { m_downloadFile.remove(); };

    if (reply->error() != QNetworkReply::NoError || status != 200) {
        discard();
        fail(QStringLiteral("download failed: %1").arg(reply->errorString()));
        return;
    }
    setState(QStringLiteral("verifying"));
    if (m_downloadedBytes != m_manifest->file.size) {
        discard();
        fail(QStringLiteral("download size mismatch"));
        return;
    }
    const auto digest = QString::fromLatin1(m_downloadHash.result().toHex());
    if (digest != m_manifest->file.sha256) {
        discard();
        fail(QStringLiteral("download sha256 mismatch"));
        return;
    }

    QString archivePath = m_downloadFile.fileName();
    archivePath.chop(qsizetype(qstrlen(".part")));
    QFile::remove(archivePath);
    if (!m_downloadFile.rename(archivePath)) {
        discard();
        fail(QStringLiteral("cannot finalize %1").arg(archivePath));
        return;
    }
    if (m_config.singleFileArtifact) {
        onImageDownloaded(archivePath);
        return;
    }
    beginExtraction(archivePath);
}

void UpdateManager::onImageDownloaded(const QString& imagePath)
{
    QFile image(imagePath);
    if (!image.open(QFile::ReadOnly)) {
        fail(QStringLiteral("cannot reopen %1").arg(imagePath));
        return;
    }
    const QByteArray header = image.read(11);
    image.close();
    if (!header.startsWith(QByteArrayLiteral("\x7f"
                                             "ELF"))
        || header.mid(8, 3) != QByteArrayLiteral("AI\x02")) {
        QFile::remove(imagePath);
        fail(QStringLiteral("downloaded file is not an AppImage"));
        return;
    }
    QFile::setPermissions(imagePath,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
            | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther
            | QFile::ExeOther);
    m_payloadPath = imagePath;
    m_canApply = m_config.layout.kind == InstallLayout::Kind::AppImage
        && stagingInsideRoot();
    m_applyHint = m_canApply
        ? QString { }
        : (m_config.layout.kind == InstallLayout::Kind::AppImage
                  ? QStringLiteral("The install directory is not writable; "
                                   "replace the AppImage in %1 manually.")
                  : QStringLiteral("This build does not run from an AppImage; "
                                   "the update was saved to %1."))
              .arg(stagingDir());
    setState(QStringLiteral("readyToApply"));
}

void UpdateManager::beginExtraction(const QString& archivePath)
{
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        fail(QStringLiteral("no tar executable found to extract the update"));
        return;
    }
    const QString extractDir = stagingDir() + QStringLiteral("/payload");
    QDir(extractDir).removeRecursively();
    if (!QDir().mkpath(extractDir)) {
        fail(QStringLiteral("cannot create %1").arg(extractDir));
        return;
    }
    setState(QStringLiteral("extracting"));

    auto* process = new QProcess(this);
    process->setProgram(tar);
    process->setArguments(
        { QStringLiteral("-xf"), archivePath, QStringLiteral("-C"),
            extractDir });
    connect(process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process, extractDir](int exitCode, QProcess::ExitStatus status) {
            process->deleteLater();
            if (status != QProcess::NormalExit || exitCode != 0) {
                fail(QStringLiteral("extraction failed: %1")
                        .arg(QString::fromUtf8(
                            process->readAllStandardError().left(500))));
                return;
            }
            onExtractionFinished(extractDir);
        });
    connect(process, &QProcess::errorOccurred, this,
        [this, process](QProcess::ProcessError) {
            process->deleteLater();
            fail(QStringLiteral("extraction failed: %1")
                    .arg(process->errorString()));
        });
    process->start();
}

void UpdateManager::onExtractionFinished(const QString& payloadDir)
{
#ifdef Q_OS_WIN
    const QString probe
        = payloadDir + QStringLiteral("/driscord_client.exe");
#else
    const QString probe
        = payloadDir + QStringLiteral("/bin/driscord_client");
#endif
    if (!QFileInfo(probe).isFile()) {
        fail(QStringLiteral("extracted archive is not a client package"));
        return;
    }
    m_payloadPath = payloadDir;
    m_canApply = stagingInsideRoot();
    m_applyHint = m_canApply
        ? QString { }
        : (m_config.layout.portable
                  ? QStringLiteral("The install directory is not writable; "
                                   "apply the archive in %1 manually.")
                  : QStringLiteral("This build does not run from a portable "
                                   "package; the update was saved to %1."))
              .arg(stagingDir());
    setState(QStringLiteral("readyToApply"));
}

void UpdateManager::applyAndRestart()
{
    if (m_state != QLatin1String("readyToApply") || !m_canApply
        || m_payloadPath.isEmpty()) {
        return;
    }
    setState(QStringLiteral("applying"));
    const QString backupDir
        = m_config.layout.rootDir + QStringLiteral("/.update/old");
    const auto result = m_config.layout.kind == InstallLayout::Kind::AppImage
        ? install_swap::applyImageFile(
              m_config.layout.appImagePath, m_payloadPath, backupDir)
        : install_swap::applyPayload(
              m_config.layout.rootDir, m_payloadPath, backupDir);
    if (!result.ok) {
        fail(QStringLiteral("applying the update failed: %1 — the previous "
                            "files were restored")
                .arg(result.error));
        return;
    }
    if (!QProcess::startDetached(
            m_config.layout.relaunchPath, { }, m_config.layout.rootDir)) {
        fail(QStringLiteral("the new version is installed but could not be "
                            "started; restart manually"));
        return;
    }
    QCoreApplication::quit();
}

void UpdateManager::dismissNotice()
{
    if (!m_latestVersion.isEmpty()) {
        QSettings().setValue(kDismissedVersionKey, m_latestVersion);
    }
    if (m_noticeVisible) {
        m_noticeVisible = false;
        emit noticeVisibleChanged();
    }
}
