#include "update/Minisign.h"

#include "support/minisign_signer.h"

#include <QFile>
#include <QTest>

namespace {

const QByteArray kProductionKeyLine
    = QByteArrayLiteral("RWQRKanqjc6QBQqzMopoiyl0ZJ0I7X5dwnEx1JSOO9gjgEyo4EIWboYw");

QByteArray readFixture(const char* name)
{
    QFile file(QStringLiteral(TEST_DATA_DIR "/") + QLatin1String(name));
    if (!file.open(QFile::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

}

class TestMinisign : public QObject {
    Q_OBJECT

private slots:
    void parsesTheProductionKeyLine()
    {
        const auto key = minisign::parsePublicKey(kProductionKeyLine);
        QVERIFY(key.has_value());
        QCOMPARE(key->keyId.toHex().toUpper(),
            QByteArrayLiteral("1129A9EA8DCE9005"));
        QCOMPARE(key->key.size(), 32);
    }

    void parsesAFullPubFile()
    {
        const auto key = minisign::parsePublicKey(
            QByteArrayLiteral("untrusted comment: minisign public key\n")
            + kProductionKeyLine + '\n');
        QVERIFY(key.has_value());
    }

    void rejectsForeignKeyMaterial()
    {
        QVERIFY(!minisign::parsePublicKey(QByteArrayLiteral("not base64!")));
        QVERIFY(!minisign::parsePublicKey(
            QByteArrayLiteral("dG9vIHNob3J0")));
        QVERIFY(!minisign::parsePublicKey(
            kProductionKeyLine + '\n' + kProductionKeyLine));
    }

    void verifiesTheLiveChannelSignature()
    {
        const auto manifest = readFixture("latest.json");
        const auto signature = readFixture("latest.json.minisig");
        QVERIFY(!manifest.isEmpty());
        QVERIFY(!signature.isEmpty());
        const auto key = minisign::parsePublicKey(kProductionKeyLine);
        QVERIFY(key.has_value());

        const auto result
            = minisign::verifyDetached(manifest, signature, { *key });
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.trustedComment,
            QStringLiteral("timestamp:1787859783\tfile:latest.json"));
    }

    void roundTripsWithTheTestSigner()
    {
        const auto pair = test_support::MinisignKeyPair::generate();
        const auto key = minisign::parsePublicKey(pair.publicKeyLine());
        QVERIFY(key.has_value());

        const QByteArray content = QByteArrayLiteral("{\"schema\": 1}\n");
        const auto signature = pair.signContent(content);
        const auto result
            = minisign::verifyDetached(content, signature, { *key });
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.trustedComment,
            QStringLiteral("timestamp:0\tfile:latest.json"));
    }

    void rejectsTamperedContent()
    {
        const auto pair = test_support::MinisignKeyPair::generate();
        const auto key = minisign::parsePublicKey(pair.publicKeyLine());
        const auto signature
            = pair.signContent(QByteArrayLiteral("original"));
        const auto result = minisign::verifyDetached(
            QByteArrayLiteral("tampered"), signature, { *key });
        QVERIFY(!result.ok);
    }

    void rejectsATamperedTrustedComment()
    {
        const auto pair = test_support::MinisignKeyPair::generate();
        const auto key = minisign::parsePublicKey(pair.publicKeyLine());
        const QByteArray content = QByteArrayLiteral("content");
        auto signature = pair.signContent(content);
        signature.replace("timestamp:0", "timestamp:9");
        const auto result
            = minisign::verifyDetached(content, signature, { *key });
        QVERIFY(!result.ok);
    }

    void rejectsAnUntrustedKeyId()
    {
        const auto signer = test_support::MinisignKeyPair::generate();
        const auto other = test_support::MinisignKeyPair::generate(
            QByteArrayLiteral("\x09\x09\x09\x09\x09\x09\x09\x09"));
        const auto trusted = minisign::parsePublicKey(other.publicKeyLine());
        const QByteArray content = QByteArrayLiteral("content");
        const auto result = minisign::verifyDetached(
            content, signer.signContent(content), { *trusted });
        QVERIFY(!result.ok);
    }

    void findsTheKeyAnywhereInTheTrustList()
    {
        const auto oldKey = test_support::MinisignKeyPair::generate(
            QByteArrayLiteral("\x09\x09\x09\x09\x09\x09\x09\x09"));
        const auto newKey = test_support::MinisignKeyPair::generate();
        const QByteArray content = QByteArrayLiteral("content");
        const auto result = minisign::verifyDetached(content,
            newKey.signContent(content),
            { *minisign::parsePublicKey(oldKey.publicKeyLine()),
                *minisign::parsePublicKey(newKey.publicKeyLine()) });
        QVERIFY2(result.ok, qPrintable(result.error));
    }

    void rejectsMalformedSignatureFiles()
    {
        const auto pair = test_support::MinisignKeyPair::generate();
        const auto key = minisign::parsePublicKey(pair.publicKeyLine());
        const QByteArray content = QByteArrayLiteral("content");

        QVERIFY(!minisign::verifyDetached(content, { }, { *key }).ok);
        QVERIFY(!minisign::verifyDetached(content,
            QByteArrayLiteral("just\ntwo lines"), { *key })
                .ok);

        auto signature = pair.signContent(content);
        auto lines = signature.split('\n');
        lines[1] = "!!!not base64!!!";
        QVERIFY(!minisign::verifyDetached(content, lines.join('\n'), { *key })
                .ok);
    }
};

QTEST_APPLESS_MAIN(TestMinisign)
#include "test_minisign.moc"
