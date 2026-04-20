#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>

#include "app/AppConfig.h"
#include "app/AppState.h"
#include "app/DriscordBridge.h"
#include "app/FrameProvider.h"
#include "api/ApiClient.h"
#include "api/AuthManager.h"
#include "api/SessionStore.h"
#include "api/ServerRepository.h"
#include "api/UserRepository.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("Driscord");
    app.setApplicationVersion("0.3.0");
    app.setOrganizationName("driscord");

    AppConfig cfg = AppConfig::load();

    auto* apiClient    = new ApiClient(&app);
    apiClient->setBaseUrl(cfg.apiBaseUrl());

    auto* sessionStore = new SessionStore(&app);
    auto* authManager  = new AuthManager(apiClient, sessionStore, &app);
    auto* serverRepo   = new ServerRepository(apiClient, &app);
    auto* userRepo     = new UserRepository(apiClient, &app);
    auto* bridge       = new DriscordBridge(&app);
    auto* frameProvider= new FrameProvider;
    auto* appState     = new AppState(authManager, serverRepo, userRepo, bridge,
                                      cfg.signalingUrl(), cfg.apiBaseUrl(), &app);

    bridge->setFrameProvider(frameProvider);

    // Configure TURN servers
    for (const auto& t : cfg.turnServers)
        bridge->addTurnServer(t.url, t.user, t.pass);

    QQmlApplicationEngine engine;
    engine.addImageProvider("frames", frameProvider);
    engine.rootContext()->setContextProperty("appState",    appState);
    engine.rootContext()->setContextProperty("authManager", authManager);
    engine.rootContext()->setContextProperty("bridge",      bridge);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("driscord", "Main");

    return app.exec();
}
