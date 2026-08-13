#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <rtc/rtc.hpp>

#include <iostream>
#include <string_view>

#include "driscord/client_build_config.hpp"
#include "driscord/version.hpp"
#include "api/ApiClient.h"
#include "api/AuthManager.h"
#include "api/ServerRepository.h"
#include "api/SessionStore.h"
#include "api/UserRepository.h"
#include "app/AppConfig.h"
#include "app/AppState.h"
#include "app/AvatarTintProvider.h"
#include "app/DriscordBridge.h"
#include "app/FrameProvider.h"
#include "app/ThumbnailProvider.h"

namespace {

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

} // namespace

int main(int argc, char* argv[])
{
    if (const int earlyExitCode = handleEarlyArguments(argc, argv); earlyExitCode >= 0)
        return earlyExitCode;

    // Honor exact OS scale factors (incl. fractional like 1.25/1.5/1.75) so the
    // UI is the same physical size on FullHD@100% as on 4K@200%. Must be set
    // before QGuiApplication is constructed.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    int rc = 0;
    // Scoped so the application — and with it DriscordBridge, DriscordCore and
    // the Transport's PeerConnection — is fully destroyed before rtc::Cleanup()
    // joins libdatachannel's thread pool below.
    {
        QGuiApplication app(argc, argv);
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
        auto* frameProvider = new FrameProvider;
        auto* thumbProvider = new ThumbnailProvider;
        auto* avatarTint = new AvatarTintProvider(&app);
        auto* appState = new AppState(authManager, serverRepo, userRepo, bridge,
            signalingUrl, apiBaseUrl, &app);

        bridge->setFrameProvider(frameProvider);
        bridge->setThumbnailProvider(thumbProvider);

        QQmlApplicationEngine engine;
        engine.addImageProvider("frames", frameProvider);
        engine.addImageProvider("thumbs", thumbProvider);
        engine.rootContext()->setContextProperty("appState", appState);
        engine.rootContext()->setContextProperty("authManager", authManager);
        engine.rootContext()->setContextProperty("bridge", bridge);
        engine.rootContext()->setContextProperty("avatarTint", avatarTint);
        engine.rootContext()->setContextProperty(
            "defaultScreenFps", cfg.screenFps);

        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
        engine.loadFromModule("driscord", "Main");

        // Release smoke jobs exercise the real installed binary, Qt platform
        // plugin and QML module graph without requiring credentials or a
        // display. A failed root object still wins through the queued -1 exit.
        if (smokeTest && !engine.rootObjects().isEmpty())
            QTimer::singleShot(0, &app, &QCoreApplication::quit);

        rc = app.exec();

        // The QML engine owns the image providers and is destroyed at the end
        // of this scope, before `app` destroys the bridge. Media threads are
        // still delivering frames into those providers until this call, so the
        // core has to be torn down first.
        bridge->shutdown();
    }

    // Joins libdatachannel's thread pool while the runtime is still alive;
    // without it, its static teardown races the process exit.
    rtc::Cleanup().wait();
    return rc;
}
