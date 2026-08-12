#include "app/AppConfig.h"

#include <QTest>

// AppConfig::load() reads the working directory and the platform config dir, so
// it is deliberately not exercised here. The URL normalisation around it is
// pure, and it is what decides whether the client talks to the right endpoint
// when config.json spells a host without a scheme.
class TestAppConfig : public QObject {
    Q_OBJECT

private slots:
    void signalingUrlAddsDefaultScheme()
    {
        AppConfig cfg;
        QCOMPARE(cfg.signalingUrl(), QStringLiteral("ws://localhost:9001"));
    }

    void signalingUrlKeepsExplicitSchemeAndDropsTrailingSlashes()
    {
        AppConfig cfg;
        cfg.server = "WSS://voice.example.com//";
        QCOMPARE(cfg.signalingUrl(), QStringLiteral("WSS://voice.example.com"));
    }

    void apiBaseUrlAddsDefaultScheme()
    {
        AppConfig cfg;
        QCOMPARE(cfg.apiBaseUrl(), QStringLiteral("http://localhost:9002"));
    }

    void apiBaseUrlKeepsHttps()
    {
        AppConfig cfg;
        cfg.api = "https://api.example.com/";
        QCOMPARE(cfg.apiBaseUrl(), QStringLiteral("https://api.example.com"));
    }
};

QTEST_APPLESS_MAIN(TestAppConfig)
#include "test_app_config.moc"
