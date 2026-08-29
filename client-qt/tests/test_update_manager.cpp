#include "update/UpdateManager.h"

#include "support/http_fixture.h"
#include "support/minisign_signer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using test_support::HttpFixture;
using test_support::HttpResponse;
using test_support::MinisignKeyPair;

namespace {

bool writeFile(const QString& path, const QByteArray& content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    if (!file.open(QFile::WriteOnly)) {
        return false;
    }
    return file.write(content) == content.size();
}

bool buildInstallTree(const QString& root, const QByteArray& stamp)
{
    if (!writeFile(root + "/driscord", "launcher " + stamp)
        || !writeFile(root + "/bin/driscord_client", "client " + stamp)
        || !writeFile(root + "/lib/libcore.so", "core " + stamp)) {
        return false;
    }
    return QFile::setPermissions(root + "/driscord",
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

struct TestChannel {
    QByteArray manifest;
    QByteArray signature;
    QByteArray archive;
    QString archiveName;
};

TestChannel makeChannel(const QString& workDir, const MinisignKeyPair& key,
    const QString& version, const QByteArray& corruptArchiveWith = { })
{
    TestChannel channel;
    const QString tree = workDir + "/tree";
    if (!buildInstallTree(tree, "new")) {
        return channel;
    }
    channel.archiveName = QStringLiteral(
        "driscord-client-%1-linux-x86_64.tar.gz")
                              .arg(version);
    const QString archivePath = workDir + '/' + channel.archiveName;
    QProcess tar;
    tar.setWorkingDirectory(tree);
    tar.start(QStandardPaths::findExecutable(QStringLiteral("tar")),
        { QStringLiteral("-czf"), archivePath, QStringLiteral("driscord"),
            QStringLiteral("bin"), QStringLiteral("lib") });
    if (!tar.waitForFinished(10000) || tar.exitCode() != 0) {
        return channel;
    }
    QFile archive(archivePath);
    if (!archive.open(QFile::ReadOnly)) {
        return channel;
    }
    channel.archive = archive.readAll();

    const auto sha256 = QCryptographicHash::hash(
        channel.archive, QCryptographicHash::Sha256)
                            .toHex();
    QJsonObject file {
        { QStringLiteral("name"), channel.archiveName },
        { QStringLiteral("sha256"), QString::fromLatin1(sha256) },
        { QStringLiteral("size"), double(channel.archive.size()) },
        { QStringLiteral("url"), version + '/' + channel.archiveName },
    };
    QJsonObject manifest {
        { QStringLiteral("schema"), 1 },
        { QStringLiteral("project"), QStringLiteral("driscord") },
        { QStringLiteral("channel"), QStringLiteral("stable") },
        { QStringLiteral("target"), QStringLiteral("linux/amd64") },
        { QStringLiteral("version"), version },
        { QStringLiteral("tag"), 'v' + version },
        { QStringLiteral("commit"), QStringLiteral("0") },
        { QStringLiteral("source"), QStringLiteral("test") },
        { QStringLiteral("published_at"),
            QStringLiteral("2026-01-01T00:00:00Z") },
        { QStringLiteral("files"), QJsonArray { file } },
    };
    channel.manifest = QJsonDocument(manifest).toJson();
    channel.signature = key.signContent(channel.manifest);
    if (!corruptArchiveWith.isEmpty()) {
        channel.archive.replace(0, corruptArchiveWith.size(),
            corruptArchiveWith);
    }
    return channel;
}

TestChannel makeSingleFileChannel(const test_support::MinisignKeyPair& key,
    const QString& version, const QByteArray& imageBytes)
{
    TestChannel channel;
    channel.archive = imageBytes;
    channel.archiveName
        = QStringLiteral("driscord-client-%1-linux-x86_64.AppImage")
              .arg(version);
    const auto sha256 = QCryptographicHash::hash(
        channel.archive, QCryptographicHash::Sha256)
                            .toHex();
    QJsonObject file {
        { QStringLiteral("name"), channel.archiveName },
        { QStringLiteral("sha256"), QString::fromLatin1(sha256) },
        { QStringLiteral("size"), double(channel.archive.size()) },
        { QStringLiteral("url"), version + '/' + channel.archiveName },
    };
    QJsonObject manifest {
        { QStringLiteral("schema"), 1 },
        { QStringLiteral("project"), QStringLiteral("driscord") },
        { QStringLiteral("channel"), QStringLiteral("stable") },
        { QStringLiteral("target"), QStringLiteral("linux/amd64") },
        { QStringLiteral("version"), version },
        { QStringLiteral("tag"), 'v' + version },
        { QStringLiteral("commit"), QStringLiteral("0") },
        { QStringLiteral("source"), QStringLiteral("test") },
        { QStringLiteral("published_at"),
            QStringLiteral("2026-01-01T00:00:00Z") },
        { QStringLiteral("files"), QJsonArray { file } },
    };
    channel.manifest = QJsonDocument(manifest).toJson();
    channel.signature = key.signContent(channel.manifest);
    return channel;
}

}

class TestUpdateManager : public QObject {
    Q_OBJECT

    QString tarPath() const
    {
        return QStandardPaths::findExecutable(QStringLiteral("tar"));
    }

    UpdateManagerConfig baseConfig(const HttpFixture& fixture,
        const MinisignKeyPair& key, const QString& rootDir,
        const QString& fallbackDir) const
    {
        UpdateManagerConfig config;
        config.baseUrl = fixture.baseUrl();
        config.channel = QStringLiteral("stable");
        config.pathTarget = QStringLiteral("linux-amd64");
        config.manifestTarget = QStringLiteral("linux/amd64");
        config.archiveSuffix = QStringLiteral(".tar.gz");
        config.currentVersionCore = QStringLiteral("1.0.0");
        config.currentVersionDisplay = QStringLiteral("1.0.0");
        config.trustedKeys
            = { *minisign::parsePublicKey(key.publicKeyLine()) };
        config.allowAutomaticChecks = false;
        if (QFileInfo(rootDir).isDir()) {
            config.layout = { InstallLayout::Kind::WindowsFlat, true,
                QFileInfo(rootDir).absoluteFilePath(),
                rootDir + "/bin/driscord_client", { } };
        }
        config.fallbackStagingDir = fallbackDir;
        return config;
    }

    UpdateManagerConfig appImageConfig(const HttpFixture& fixture,
        const MinisignKeyPair& key, const QString& imagePath,
        const QString& fallbackDir) const
    {
        auto config = baseConfig(fixture, key, { }, fallbackDir);
        config.archiveSuffix = QStringLiteral(".AppImage");
        config.singleFileArtifact = true;
        config.layout = detectInstallLayout(
            QStringLiteral("/irrelevant/exe"), imagePath);
        return config;
    }

    static QByteArray fakeAppImageBytes()
    {
        QByteArray bytes = QByteArrayLiteral("\x7f"
                                             "ELF\x02\x01\x01\x00"
                                             "AI\x02\x00");
        bytes.append(2048, 'x');
        return bytes;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(
            QStringLiteral("driscord-test"));
        QCoreApplication::setApplicationName(
            QStringLiteral("DriscordUpdateTest"));
        if (tarPath().isEmpty()) {
            QSKIP("no tar executable on this host");
        }
    }

    void init() { QSettings().clear(); }

    void fullPipelineReachesReadyToApply()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.filePath("install");
        QVERIFY(buildInstallTree(root, "old"));

        const auto key = MinisignKeyPair::generate();
        const auto channel
            = makeChannel(temp.filePath("channel"), key, "9.9.9");
        QVERIFY(!channel.archive.isEmpty());

        HttpFixture fixture;
        QVERIFY(fixture.listening());
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });
        fixture.enqueue(
            { 200, channel.archive, "application/octet-stream" });

        UpdateManager manager(
            baseConfig(fixture, key, root, temp.filePath("fallback")));
        QCOMPARE(manager.state(), QStringLiteral("idle"));

        manager.checkForUpdates();
        QCOMPARE(manager.state(), QStringLiteral("checking"));
        QTRY_COMPARE(manager.state(), QStringLiteral("updateAvailable"));
        QCOMPARE(manager.latestVersion(), QStringLiteral("9.9.9"));
        QVERIFY(manager.noticeVisible());

        manager.downloadUpdate();
        QTRY_COMPARE(manager.state(), QStringLiteral("readyToApply"));
        QVERIFY(manager.canApply());
        QVERIFY(manager.applyHint().isEmpty());

        QCOMPARE(fixture.requests().size(), 3);
        QCOMPARE(fixture.requests()[0].path,
            QByteArrayLiteral("/stable/linux-amd64/latest.json"));
        QCOMPARE(fixture.requests()[1].path,
            QByteArrayLiteral("/stable/linux-amd64/latest.json.minisig"));
        QCOMPARE(fixture.requests()[2].path,
            "/stable/linux-amd64/9.9.9/" + channel.archiveName.toUtf8());

        QVERIFY(QFileInfo(
            root + "/.update/payload/bin/driscord_client")
                .isFile());
    }

    void tamperedSignatureStopsBeforeTheArchive()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.filePath("install");
        QVERIFY(buildInstallTree(root, "old"));

        const auto key = MinisignKeyPair::generate();
        auto channel = makeChannel(temp.filePath("channel"), key, "9.9.9");
        channel.signature.replace("timestamp:0", "timestamp:1");

        HttpFixture fixture;
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });

        UpdateManager manager(
            baseConfig(fixture, key, root, temp.filePath("fallback")));
        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("error"));
        QVERIFY(manager.errorText().contains(
            QStringLiteral("signature rejected")));
        QCOMPARE(fixture.requests().size(), 2);
    }

    void sha256MismatchDiscardsTheDownload()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.filePath("install");
        QVERIFY(buildInstallTree(root, "old"));

        const auto key = MinisignKeyPair::generate();
        const auto channel = makeChannel(temp.filePath("channel"), key,
            "9.9.9", QByteArrayLiteral("XXXX"));
        QVERIFY(!channel.archive.isEmpty());

        HttpFixture fixture;
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });
        fixture.enqueue(
            { 200, channel.archive, "application/octet-stream" });

        UpdateManager manager(
            baseConfig(fixture, key, root, temp.filePath("fallback")));
        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("updateAvailable"));
        manager.downloadUpdate();
        QTRY_COMPARE(manager.state(), QStringLiteral("error"));
        QVERIFY(manager.errorText().contains(QStringLiteral("sha256")));
        QVERIFY(!QFileInfo::exists(root + "/.update/download/"
            + channel.archiveName + QStringLiteral(".part")));
    }

    void currentVersionIsUpToDateWithoutADownload()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.filePath("install");
        QVERIFY(buildInstallTree(root, "old"));

        const auto key = MinisignKeyPair::generate();
        const auto channel
            = makeChannel(temp.filePath("channel"), key, "1.0.0");

        HttpFixture fixture;
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });

        UpdateManager manager(
            baseConfig(fixture, key, root, temp.filePath("fallback")));
        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("upToDate"));
        QVERIFY(!manager.noticeVisible());
        manager.downloadUpdate();
        QCOMPARE(manager.state(), QStringLiteral("upToDate"));
        QCOMPARE(fixture.requests().size(), 2);
    }

    void aSecondCheckWhileCheckingIsANoOp()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.filePath("install");
        QVERIFY(buildInstallTree(root, "old"));

        const auto key = MinisignKeyPair::generate();
        const auto channel
            = makeChannel(temp.filePath("channel"), key, "1.0.0");

        HttpFixture fixture;
        fixture.enqueue({ 200, channel.manifest, "application/json", false, 100 });
        fixture.enqueue({ 200, channel.signature, "text/plain" });

        UpdateManager manager(
            baseConfig(fixture, key, root, temp.filePath("fallback")));
        manager.checkForUpdates();
        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("upToDate"));
        QCOMPARE(fixture.requests().size(), 2);
    }

    void nonPortableLayoutDownloadsButCannotApply()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const auto key = MinisignKeyPair::generate();
        const auto channel
            = makeChannel(temp.filePath("channel"), key, "9.9.9");
        QVERIFY(!channel.archive.isEmpty());

        HttpFixture fixture;
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });
        fixture.enqueue(
            { 200, channel.archive, "application/octet-stream" });

        auto config = baseConfig(
            fixture, key, temp.filePath("nowhere"), temp.filePath("fallback"));
        QVERIFY(!config.layout.portable);

        UpdateManager manager(config);
        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("updateAvailable"));
        manager.downloadUpdate();
        QTRY_COMPARE(manager.state(), QStringLiteral("readyToApply"));
        QVERIFY(!manager.canApply());
        QVERIFY(manager.applyHint().contains(temp.filePath("fallback")));

        manager.applyAndRestart();
        QCOMPARE(manager.state(), QStringLiteral("readyToApply"));
    }

    void dismissingTheNoticeSticksAcrossChecks()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.filePath("install");
        QVERIFY(buildInstallTree(root, "old"));

        const auto key = MinisignKeyPair::generate();
        const auto channel
            = makeChannel(temp.filePath("channel"), key, "9.9.9");

        HttpFixture fixture;
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });

        UpdateManager manager(
            baseConfig(fixture, key, root, temp.filePath("fallback")));
        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("updateAvailable"));
        QVERIFY(manager.noticeVisible());
        manager.dismissNotice();
        QVERIFY(!manager.noticeVisible());

        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("updateAvailable"));
        QVERIFY(!manager.noticeVisible());
    }

    void emptyBaseUrlDisablesEverything()
    {
        UpdateManagerConfig config;
        config.currentVersionCore = QStringLiteral("1.0.0");
        UpdateManager manager(config);
        QCOMPARE(manager.state(), QStringLiteral("disabled"));
        manager.checkForUpdates();
        QCOMPARE(manager.state(), QStringLiteral("disabled"));
    }

    void appImagePipelineReachesReadyToApply()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString image = temp.filePath("apps/Driscord.AppImage");
        QVERIFY(writeFile(image, "old image"));

        const auto key = MinisignKeyPair::generate();
        const auto channel = makeSingleFileChannel(
            key, QStringLiteral("9.9.9"), fakeAppImageBytes());

        HttpFixture fixture;
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });
        fixture.enqueue(
            { 200, channel.archive, "application/octet-stream" });

        UpdateManager manager(
            appImageConfig(fixture, key, image, temp.filePath("fallback")));
        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("updateAvailable"));
        manager.downloadUpdate();
        QTRY_COMPARE(manager.state(), QStringLiteral("readyToApply"));
        QVERIFY(manager.canApply());
        QVERIFY(manager.applyHint().isEmpty());

        const QString downloaded = temp.filePath(
            "apps/.update/download/" + channel.archiveName);
        QVERIFY(QFileInfo(downloaded).isFile());
        QVERIFY(QFileInfo(downloaded).isExecutable());
    }

    void appImageWithABadHeaderIsRejected()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString image = temp.filePath("apps/Driscord.AppImage");
        QVERIFY(writeFile(image, "old image"));

        const auto key = MinisignKeyPair::generate();
        const auto channel = makeSingleFileChannel(
            key, QStringLiteral("9.9.9"),
            QByteArray("definitely not an ELF").append(2048, 'x'));

        HttpFixture fixture;
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });
        fixture.enqueue(
            { 200, channel.archive, "application/octet-stream" });

        UpdateManager manager(
            appImageConfig(fixture, key, image, temp.filePath("fallback")));
        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("updateAvailable"));
        manager.downloadUpdate();
        QTRY_COMPARE(manager.state(), QStringLiteral("error"));
        QVERIFY(manager.errorText().contains(QStringLiteral("AppImage")));
    }

    void nonAppImageRunDownloadsButCannotApply()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const auto key = MinisignKeyPair::generate();
        const auto channel = makeSingleFileChannel(
            key, QStringLiteral("9.9.9"), fakeAppImageBytes());

        HttpFixture fixture;
        fixture.enqueue({ 200, channel.manifest, "application/json" });
        fixture.enqueue({ 200, channel.signature, "text/plain" });
        fixture.enqueue(
            { 200, channel.archive, "application/octet-stream" });

        auto config = appImageConfig(
            fixture, key, { }, temp.filePath("fallback"));
        QCOMPARE(config.layout.kind, InstallLayout::Kind::None);

        UpdateManager manager(config);
        manager.checkForUpdates();
        QTRY_COMPARE(manager.state(), QStringLiteral("updateAvailable"));
        manager.downloadUpdate();
        QTRY_COMPARE(manager.state(), QStringLiteral("readyToApply"));
        QVERIFY(!manager.canApply());
        QVERIFY(manager.applyHint().contains(temp.filePath("fallback")));
        manager.applyAndRestart();
        QCOMPARE(manager.state(), QStringLiteral("readyToApply"));
    }
};

QTEST_GUILESS_MAIN(TestUpdateManager)
#include "test_update_manager.moc"
