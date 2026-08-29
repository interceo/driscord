#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <compare>
#include <optional>

struct VersionTriple {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static std::optional<VersionTriple> parse(const QString& text);

    auto operator<=>(const VersionTriple&) const = default;
    QString toString() const;
};

struct UpdateFileEntry {
    QString name;
    QString sha256;
    qint64 size = 0;
    QString url;
};

struct UpdateManifest {
    VersionTriple version;
    QString channel;
    QString target;
    UpdateFileEntry file;
};

struct ManifestParseResult {
    std::optional<UpdateManifest> manifest;
    QString error;
};

ManifestParseResult parseUpdateManifest(const QByteArray& json,
    const QString& expectedChannel, const QString& expectedTarget,
    const QString& archiveSuffix);
