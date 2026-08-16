#include "app/AppConfig.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestAppConfig : public QObject {
    Q_OBJECT

private slots:
    void defaultsAreApplicationSettingsOnly()
    {
        AppConfig cfg;
        QCOMPARE(cfg.screenFps, 60);
        QVERIFY(cfg.iceServers.isEmpty());
    }

    void loadReadsUserSettingsFromPlatformDirectory()
    {
        QTemporaryDir configHome;
        QVERIFY(configHome.isValid());
        const QString configDirectory = configHome.filePath("driscord");
        QVERIFY(QDir().mkpath(configDirectory));

        QFile config(configDirectory + "/config.json");
        QVERIFY(config.open(QFile::WriteOnly));
        config.write("{\n"
                     "  \"server\": \"wss:\\/\\/ignored.example\",\n"
                     "  \"api\": \"https:\\/\\/ignored.example\",\n"
                     "  \"screen_fps\": 30,\n"
                     "  \"stun_servers\": [\"stun:stun.example:3478\"]\n"
                     "}\n");
        config.close();

        const bool hadConfigHome = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        const QByteArray oldConfigHome = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
        const AppConfig cfg = AppConfig::load();
        if (hadConfigHome)
            qputenv("XDG_CONFIG_HOME", oldConfigHome);
        else
            qunsetenv("XDG_CONFIG_HOME");

        QCOMPARE(cfg.screenFps, 30);
        QCOMPARE(cfg.iceServers.size(), 1);
        QCOMPARE(cfg.iceServers.front().url,
            QStringLiteral("stun:stun.example:3478"));
    }
};

QTEST_APPLESS_MAIN(TestAppConfig)
#include "test_app_config.moc"
