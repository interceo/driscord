#include "api/SessionStore.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

// The session store persists the refresh token — the credential that keeps a
// user logged in across restarts — so its round-trip and clear semantics are
// security-relevant and were untested. QStandardPaths test mode redirects
// QSettings to a throwaway location so the test never touches real config.
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
        // Each test starts from an empty store.
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
        // A fresh instance (a restart) must see the persisted session.
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
        // A stored-but-empty refresh token must not read back as a valid
        // session — otherwise a blank credential would look logged in.
        SessionStore store;
        store.save(SessionData { "", "dave", 5 });
        QVERIFY(!store.load().has_value());
    }
};

QTEST_GUILESS_MAIN(TestSessionStore)
#include "test_session_store.moc"
