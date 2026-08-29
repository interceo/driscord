#pragma once
#include "InstallLayout.h"
#include "Minisign.h"
#include "UpdateManifest.h"

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <cstdint>
#include <functional>

class QNetworkReply;

struct UpdateManagerConfig {
    QString baseUrl;
    QString channel;
    QString pathTarget;
    QString manifestTarget;
    QString archiveSuffix;
    bool singleFileArtifact = false;
    QString currentVersionCore;
    QString currentVersionDisplay;
    QList<minisign::PublicKey> trustedKeys;
    bool allowAutomaticChecks = false;
    InstallLayout layout;
    QString fallbackStagingDir;
};

class UpdateManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress
            NOTIFY downloadProgressChanged)
    Q_PROPERTY(bool canApply READ canApply NOTIFY stateChanged)
    Q_PROPERTY(QString applyHint READ applyHint NOTIFY stateChanged)
    Q_PROPERTY(bool noticeVisible READ noticeVisible NOTIFY noticeVisibleChanged)
public:
    explicit UpdateManager(UpdateManagerConfig config,
        QObject* parent = nullptr);

    void startBackgroundTasks();

    QString state() const { return m_state; }
    QString currentVersion() const { return m_config.currentVersionDisplay; }
    QString latestVersion() const { return m_latestVersion; }
    QString errorText() const { return m_errorText; }
    double downloadProgress() const { return m_downloadProgress; }
    bool canApply() const { return m_canApply; }
    QString applyHint() const { return m_applyHint; }
    bool noticeVisible() const { return m_noticeVisible; }

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void applyAndRestart();
    Q_INVOKABLE void dismissNotice();

signals:
    void stateChanged();
    void downloadProgressChanged();
    void noticeVisibleChanged();

private:
    void setState(const QString& state);
    void fail(const QString& error);
    void fetchSignatureAndVerify(const QByteArray& manifestBytes);
    void onManifestVerified(const QByteArray& manifestBytes);
    void beginDownload();
    void onDownloadFinished(QNetworkReply* reply);
    void onImageDownloaded(const QString& imagePath);
    void beginExtraction(const QString& archivePath);
    void onExtractionFinished(const QString& payloadDir);
    QNetworkReply* fetchSmall(const QUrl& url, qint64 maxBytes,
        std::function<void(QByteArray)> onSuccess);
    QUrl manifestUrl() const;
    QString stagingDir() const;
    bool stagingInsideRoot() const;

    UpdateManagerConfig m_config;
    VersionTriple m_currentVersion;
    QNetworkAccessManager m_nam;
    QTimer m_periodicTimer;
    QTimer m_downloadWatchdog;
    std::uint64_t m_generation = 0;
    QNetworkReply* m_activeReply = nullptr;

    QString m_state;
    QString m_latestVersion;
    QString m_errorText;
    double m_downloadProgress = 0.0;
    bool m_canApply = false;
    QString m_applyHint;
    bool m_noticeVisible = false;

    std::optional<UpdateManifest> m_manifest;
    QFile m_downloadFile;
    QCryptographicHash m_downloadHash { QCryptographicHash::Sha256 };
    qint64 m_downloadedBytes = 0;
    QString m_payloadPath;
};
