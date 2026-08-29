#include "api/SessionStore.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

class TestSessionStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("driscord-test");
        QCoreApplication::setApplicationName("session-store-test");
    }

    void init()
    {
        SessionStore store;
        store.clear();
    }

    void loadReturnsNulloptWhenEmpty()
    {
        SessionStore store;
        QVERIFY(!store.load().has_value());
    }

    void saveThenLoadRoundTrips()
    {
        SessionStore store;
        store.save(SessionData { "refresh-abc", "alice", 42 });

        const auto loaded = store.load();
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->refreshToken, QStringLiteral("refresh-abc"));
        QCOMPARE(loaded->username, QStringLiteral("alice"));
        QCOMPARE(loaded->userId, 42);
    }

    void persistsAcrossStoreInstances()
    {
        {
            SessionStore writer;
            writer.save(SessionData { "refresh-xyz", "bob", 7 });
        }
        SessionStore reader;
        const auto loaded = reader.load();
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->refreshToken, QStringLiteral("refresh-xyz"));
        QCOMPARE(loaded->userId, 7);
    }

    void clearRemovesTheSession()
    {
        SessionStore store;
        store.save(SessionData { "refresh-tok", "carol", 1 });
        QVERIFY(store.load().has_value());

        store.clear();
        QVERIFY(!store.load().has_value());
    }

    void anEmptyTokenIsTreatedAsNoSession()
    {
        SessionStore store;
        store.save(SessionData { "", "dave", 5 });
        QVERIFY(!store.load().has_value());
    }
};

QTEST_GUILESS_MAIN(TestSessionStore)
#include "test_session_store.moc"
