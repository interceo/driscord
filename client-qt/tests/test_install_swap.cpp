#include "update/InstallLayout.h"
#include "update/InstallSwap.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

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

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

bool buildPayload(const QString& dir, const QByteArray& stamp)
{
    return writeFile(dir + "/driscord", "launcher " + stamp)
        && writeFile(dir + "/bin/driscord_client", "client " + stamp)
        && writeFile(dir + "/lib/libcore.so", "core " + stamp)
        && writeFile(dir + "/lib/plugins/extra.so", "extra " + stamp)
        && writeFile(dir + "/README.txt", "readme " + stamp);
}

}

class TestInstallSwap : public QObject {
    Q_OBJECT

private slots:
    void appliesAPayloadAndPreservesUserFiles()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.filePath("install");
        QVERIFY(buildPayload(root, "old"));
        QVERIFY(writeFile(root + "/config.json", "user settings"));
        const QString payload = temp.filePath("install/.update/payload");
        QVERIFY(buildPayload(payload, "new"));

        const auto result = install_swap::applyPayload(
            root, payload, root + "/.update/old");
        QVERIFY2(result.ok, qPrintable(result.error));

        QCOMPARE(readFile(root + "/bin/driscord_client"),
            QByteArrayLiteral("client new"));
        QCOMPARE(readFile(root + "/lib/plugins/extra.so"),
            QByteArrayLiteral("extra new"));
        QCOMPARE(readFile(root + "/driscord"),
            QByteArrayLiteral("launcher new"));
        QCOMPARE(readFile(root + "/config.json"),
            QByteArrayLiteral("user settings"));
        QCOMPARE(readFile(root + "/.update/old/bin/driscord_client"),
            QByteArrayLiteral("client old"));
        QCOMPARE(readFile(root + "/.update/old/lib/libcore.so"),
            QByteArrayLiteral("core old"));
    }

    void installsIntoARootMissingSomeEntries()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.filePath("install");
        QVERIFY(writeFile(root + "/config.json", "user settings"));
        const QString payload = temp.filePath("payload");
        QVERIFY(buildPayload(payload, "new"));

        const auto result
            = install_swap::applyPayload(root, payload, temp.filePath("old"));
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(readFile(root + "/bin/driscord_client"),
            QByteArrayLiteral("client new"));
    }

    void moveTreeRefusesCollisions()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(writeFile(temp.filePath("from/a.txt"), "from"));
        QVERIFY(writeFile(temp.filePath("to/a.txt"), "to"));

        const auto result = install_swap::moveTreePerFile(
            temp.filePath("from"), temp.filePath("to"));
        QVERIFY(!result.ok);
        QCOMPARE(readFile(temp.filePath("to/a.txt")), QByteArrayLiteral("to"));
    }

    void cleanupRemovesTheUpdateDir()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString root = temp.path();
        QVERIFY(writeFile(root + "/.update/old/bin/driscord_client", "x"));
        QVERIFY(writeFile(root + "/.update/download.part", "y"));

        install_swap::cleanupUpdateDir(root);
        QVERIFY(!QFileInfo::exists(root + "/.update"));
    }

    void cleanupToleratesAMissingUpdateDir()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        install_swap::cleanupUpdateDir(temp.path());
        QVERIFY(true);
    }

    void detectsAnAppImageInstall()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString image = temp.filePath("Driscord.AppImage");
        QVERIFY(writeFile(image, "fake image"));

        const auto layout = detectInstallLayout(
            temp.filePath("mount/bin/driscord_client"), image);
        QCOMPARE(layout.kind, InstallLayout::Kind::AppImage);
        QVERIFY(layout.portable);
        QCOMPARE(layout.rootDir, QFileInfo(image).absolutePath());
        QCOMPARE(layout.relaunchPath, QFileInfo(image).absoluteFilePath());
        QCOMPARE(layout.appImagePath, QFileInfo(image).absoluteFilePath());
    }

    void rejectsABogusAppImagePath()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QCOMPARE(detectInstallLayout(
                     temp.filePath("bin/driscord_client"),
                     temp.filePath("missing.AppImage"))
                     .kind,
            InstallLayout::Kind::None);
        QCOMPARE(detectInstallLayout(temp.filePath("bin/driscord_client"),
                     QStringLiteral("relative.AppImage"))
                     .kind,
            InstallLayout::Kind::None);
        QCOMPARE(
            detectInstallLayout(temp.filePath("bin/driscord_client"), temp.path())
                .kind,
            InstallLayout::Kind::None);
    }

    void rejectsABuildTreeLayout()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(writeFile(temp.filePath("client-qt/driscord_client"), "x"));
        const auto layout
            = detectInstallLayout(temp.filePath("client-qt/driscord_client"));
        QVERIFY(!layout.portable);
        QCOMPARE(layout.kind, InstallLayout::Kind::None);
    }

    void swapsAnImageFileAndKeepsABackup()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString image = temp.filePath("Driscord.AppImage");
        const QString downloaded = temp.filePath(".update/download/new.AppImage");
        QVERIFY(writeFile(image, "old image"));
        QVERIFY(writeFile(downloaded, "new image"));

        const auto result = install_swap::applyImageFile(
            image, downloaded, temp.filePath(".update/old"));
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(readFile(image), QByteArrayLiteral("new image"));
        QCOMPARE(readFile(temp.filePath(".update/old/Driscord.AppImage")),
            QByteArrayLiteral("old image"));
        QVERIFY(QFileInfo(image).isExecutable());
        QVERIFY(!QFileInfo::exists(downloaded));
    }

    void imageSwapRestoresTheOriginalOnFailure()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString image = temp.filePath("Driscord.AppImage");
        QVERIFY(writeFile(image, "old image"));

        const auto result = install_swap::applyImageFile(
            image, temp.filePath("missing.AppImage"),
            temp.filePath(".update/old"));
        QVERIFY(!result.ok);
        QCOMPARE(readFile(image), QByteArrayLiteral("old image"));
    }
};

QTEST_APPLESS_MAIN(TestInstallSwap)
#include "test_install_swap.moc"
