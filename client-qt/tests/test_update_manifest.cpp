#include "update/UpdateManifest.h"

#include <QFile>
#include <QTest>

namespace {

QByteArray liveManifestBytes()
{
    QFile file(QStringLiteral(TEST_DATA_DIR "/latest.json"));
    if (!file.open(QFile::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

QByteArray manifestJson(const QByteArray& patch = { })
{
    QByteArray base
        = "{\n"
          "  \"channel\": \"stable\",\n"
          "  \"commit\": \"0000000000000000000000000000000000000000\",\n"
          "  \"files\": [\n"
          "    {\n"
          "      \"name\": \"driscord-client-1.2.3-linux-x86_64.tar.gz\",\n"
          "      \"sha256\": \"ec15d9429fe853a0cab70503431f204cf8b19703"
          "6a7a52b714a877ea9f282d4f\",\n"
          "      \"size\": 1024,\n"
          "      \"url\": \"1.2.3/driscord-client-1.2.3-linux-x86_64.tar.gz\"\n"
          "    }\n"
          "  ],\n"
          "  \"project\": \"driscord\",\n"
          "  \"published_at\": \"2026-01-01T00:00:00Z\",\n"
          "  \"schema\": 1,\n"
          "  \"source\": \"https://example.invalid\",\n"
          "  \"tag\": \"v1.2.3\",\n"
          "  \"target\": \"linux/amd64\",\n"
          "  \"version\": \"1.2.3\"\n"
          "}";
    if (!patch.isEmpty()) {
        const int split = patch.indexOf("=>");
        base.replace(patch.left(split), patch.mid(split + 2));
    }
    return base;
}

}

class TestUpdateManifest : public QObject {
    Q_OBJECT

private slots:
    void versionTripleParsesStrictSemver()
    {
        const auto version = VersionTriple::parse(QStringLiteral("0.0.12"));
        QVERIFY(version.has_value());
        QCOMPARE(version->toString(), QStringLiteral("0.0.12"));

        QVERIFY(!VersionTriple::parse(QStringLiteral("1.2")));
        QVERIFY(!VersionTriple::parse(QStringLiteral("v1.2.3")));
        QVERIFY(!VersionTriple::parse(QStringLiteral("1.2.3-rc1")));
        QVERIFY(!VersionTriple::parse(QStringLiteral("1.2.3+g123")));
        QVERIFY(!VersionTriple::parse(QStringLiteral("01.2.3")));
        QVERIFY(!VersionTriple::parse(QString { }));
    }

    void versionTripleOrdersNumerically()
    {
        QVERIFY(*VersionTriple::parse(QStringLiteral("0.0.12"))
            > *VersionTriple::parse(QStringLiteral("0.0.9")));
        QVERIFY(*VersionTriple::parse(QStringLiteral("1.0.0"))
            > *VersionTriple::parse(QStringLiteral("0.9.9")));
        QVERIFY(*VersionTriple::parse(QStringLiteral("1.2.3"))
            == *VersionTriple::parse(QStringLiteral("1.2.3")));
    }

    void parsesTheLiveManifestShape()
    {
        const auto bytes = liveManifestBytes();
        QVERIFY(!bytes.isEmpty());
        const auto result = parseUpdateManifest(bytes,
            QStringLiteral("stable"), QStringLiteral("linux/amd64"),
            QStringLiteral(".tar.gz"));
        QVERIFY2(result.manifest.has_value(), qPrintable(result.error));
        QCOMPARE(result.manifest->version.toString(), QStringLiteral("0.0.12"));
        QCOMPARE(result.manifest->file.url,
            QStringLiteral(
                "0.0.12/driscord-client-0.0.12-linux-x86_64.tar.gz"));
        QCOMPARE(result.manifest->file.size, qint64(61429070));
    }

    void acceptsTheSyntheticManifest()
    {
        const auto result = parseUpdateManifest(manifestJson(),
            QStringLiteral("stable"), QStringLiteral("linux/amd64"),
            QStringLiteral(".tar.gz"));
        QVERIFY2(result.manifest.has_value(), qPrintable(result.error));
    }

    void rejectsForeignManifests_data()
    {
        QTest::addColumn<QByteArray>("patch");
        QTest::newRow("schema") << QByteArray("\"schema\": 1=>\"schema\": 2");
        QTest::newRow("project")
            << QByteArray("\"driscord\"=>\"other\"");
        QTest::newRow("channel")
            << QByteArray("\"stable\"=>\"beta\"");
        QTest::newRow("target")
            << QByteArray("linux/amd64=>windows/amd64");
        QTest::newRow("version")
            << QByteArray("\"version\": \"1.2.3\"=>\"version\": \"v1.2.3\"");
        QTest::newRow("sha256")
            << QByteArray("ec15d9429f=>ZZ15d9429f");
        QTest::newRow("size")
            << QByteArray("\"size\": 1024=>\"size\": 0");
        QTest::newRow("absolute-url")
            << QByteArray("\"url\": \"1.2.3/=>\"url\": \"/1.2.3/");
        QTest::newRow("scheme-url") << QByteArray(
            "\"url\": \"1.2.3/=>\"url\": \"https://evil.invalid/");
        QTest::newRow("dotdot-url")
            << QByteArray("\"url\": \"1.2.3/=>\"url\": \"../1.2.3/");
        QTest::newRow("suffix")
            << QByteArray("linux-x86_64.tar.gz\"=>linux-x86_64.zip\"");
    }

    void rejectsForeignManifests()
    {
        QFETCH(QByteArray, patch);
        const auto result = parseUpdateManifest(manifestJson(patch),
            QStringLiteral("stable"), QStringLiteral("linux/amd64"),
            QStringLiteral(".tar.gz"));
        QVERIFY(!result.manifest.has_value());
        QVERIFY(!result.error.isEmpty());
    }

    void rejectsNonJson()
    {
        const auto result = parseUpdateManifest(QByteArrayLiteral("not json"),
            QStringLiteral("stable"), QStringLiteral("linux/amd64"),
            QStringLiteral(".tar.gz"));
        QVERIFY(!result.manifest.has_value());
    }
};

QTEST_APPLESS_MAIN(TestUpdateManifest)
#include "test_update_manifest.moc"
