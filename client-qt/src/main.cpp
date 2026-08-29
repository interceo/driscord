#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <rtc/rtc.hpp>

#include <iostream>
#include <string_view>

#include "api/ApiClient.h"
#include "api/AuthManager.h"
#include "api/ServerRepository.h"
#include "api/SessionStore.h"
#include "api/UserRepository.h"
#include "app/AppConfig.h"
#include "app/AppState.h"
#include "app/AvatarTintProvider.h"
#include "app/DriscordBridge.h"
#include "app/ThumbnailProvider.h"
#include "driscord/client_build_config.hpp"
#include "driscord/version.hpp"
#include "update/UpdateManager.h"

#include <QStandardPaths>

namespace {

UpdateManagerConfig makeUpdateConfig(const AppConfig& cfg)
{
    UpdateManagerConfig update;
    update.baseUrl = QString::fromUtf8(driscord::kUpdateBaseUrl);
    update.channel = QString::fromUtf8(driscord::kUpdateChannel);
#ifdef Q_OS_WIN
    update.pathTarget = QStringLiteral("windows-amd64");
    update.manifestTarget = QStringLiteral("windows/amd64");
    update.archiveSuffix = QStringLiteral(".zip");
#else
    update.pathTarget = QStringLiteral("linux-amd64");
    update.manifestTarget = QStringLiteral("linux/amd64");
    update.archiveSuffix = QStringLiteral(".AppImage");
    update.singleFileArtifact = true;
#endif
    update.currentVersionCore = QString::fromUtf8(driscord::kVersionCore);
    update.currentVersionDisplay = QString::fromUtf8(driscord::kVersion);
    if (auto key = minisign::parsePublicKey(QByteArrayLiteral(
            "RWQRKanqjc6QBQqzMopoiyl0ZJ0I7X5dwnEx1JSOO9gjgEyo4EIWboYw"))) {
        update.trustedKeys.append(*key);
    }
    update.allowAutomaticChecks = driscord::kReleaseVersion;
    if (!driscord::kReleaseVersion) {
        if (!cfg.updateUrl.isEmpty())
            update.baseUrl = cfg.updateUrl;
        if (!cfg.updateChannel.isEmpty())
            update.channel = cfg.updateChannel;
        if (!cfg.updatePublicKey.isEmpty()) {
            if (auto key
                = minisign::parsePublicKey(cfg.updatePublicKey.toUtf8())) {
                update.trustedKeys = { *key };
            }
        }
        if (!cfg.updateUrl.isEmpty())
            update.allowAutomaticChecks = true;
    }
    update.layout = detectInstallLayout(
        QCoreApplication::applicationFilePath(),
        qEnvironmentVariable("APPIMAGE"));
    update.fallbackStagingDir
        = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/updates");
    return update;
}

int handleEarlyArguments(int argc, char* argv[])
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);

        if (argument == "--version" || argument == "-V") {
            std::cout << "Driscord " << driscord::kVersion << '\n';
            return 0;
        }

        if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: driscord [options]\n\n"
                         "Options:\n"
                         "  -h, --help       Show this help and exit\n"
                         "  -V, --version    Show the application version and exit\n"
                         "      --smoke-test Load the UI, then exit automatically\n";
            return 0;
        }
    }

    return -1;
}

}

int main(int argc, char* argv[])
{
    if (const int earlyExitCode = handleEarlyArguments(argc, argv); earlyExitCode >= 0)
        return earlyExitCode;

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    int rc = 0;
    {
        QGuiApplication app(argc, argv);
        QQuickStyle::setStyle("Basic");
        app.setApplicationName("Driscord");
        app.setApplicationVersion(QString::fromLatin1(driscord::kVersion));
        app.setOrganizationName("driscord");
        const bool smokeTest = app.arguments().contains("--smoke-test");

        AppConfig cfg = AppConfig::load();
        const QString signalingUrl = QString::fromUtf8(driscord::kSignalingUrl);
        const QString apiBaseUrl = QString::fromUtf8(driscord::kApiBaseUrl);

        auto* apiClient = new ApiClient(&app);
        apiClient->setBaseUrl(apiBaseUrl);

        auto* sessionStore = new SessionStore(&app);
        auto* authManager = new AuthManager(apiClient, sessionStore, &app);
        auto* serverRepo = new ServerRepository(apiClient, &app);
        auto* userRepo = new UserRepository(apiClient, &app);
        auto* bridge = new DriscordBridge(&app, cfg.iceServers);
        auto* thumbProvider = new ThumbnailProvider;
        auto* avatarTint = new AvatarTintProvider(&app);
        auto* appState = new AppState(authManager, serverRepo, userRepo, bridge,
            signalingUrl, apiBaseUrl, &app);
        auto* updateManager = new UpdateManager(makeUpdateConfig(cfg), &app);

        bridge->setThumbnailProvider(thumbProvider);

        QQmlApplicationEngine engine;
        engine.addImageProvider("thumbs", thumbProvider);
        engine.rootContext()->setContextProperty("appState", appState);
        engine.rootContext()->setContextProperty("authManager", authManager);
        engine.rootContext()->setContextProperty("bridge", bridge);
        engine.rootContext()->setContextProperty("avatarTint", avatarTint);
        engine.rootContext()->setContextProperty("updateManager", updateManager);
        engine.rootContext()->setContextProperty(
            "defaultScreenFps", cfg.screenFps);

        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
        engine.loadFromModule("driscord", "Main");

        if (smokeTest && !engine.rootObjects().isEmpty())
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
        if (!smokeTest)
            updateManager->startBackgroundTasks();

        rc = app.exec();

        bridge->shutdown();
    }

    rtc::Cleanup().wait();
    return rc;
}
