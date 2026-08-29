#include "UpdateManifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

constexpr qint64 kMaxArchiveBytes = qint64(1) << 30;

bool isRelativeUrl(const QString& url)
{
    if (url.isEmpty() || url.startsWith('/') || url.contains('\\')) {
        return false;
    }
    if (url.contains(':')) {
        return false;
    }
    const auto segments = url.split('/');
    for (const auto& segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String("..")) {
            return false;
        }
    }
    return true;
}

}

std::optional<VersionTriple> VersionTriple::parse(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return VersionTriple {
        match.captured(1).toInt(),
        match.captured(2).toInt(),
        match.captured(3).toInt(),
    };
}

QString VersionTriple::toString() const
{
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

ManifestParseResult parseUpdateManifest(const QByteArray& json,
    const QString& expectedChannel, const QString& expectedTarget,
    const QString& archiveSuffix)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull() || !document.isObject()) {
        return { std::nullopt,
            QStringLiteral("manifest is not a JSON object: %1")
                .arg(parseError.errorString()) };
    }
    const auto object = document.object();

    if (object.value(QLatin1String("schema")).toInt() != 1) {
        return { std::nullopt, QStringLiteral("unsupported manifest schema") };
    }
    if (object.value(QLatin1String("project")).toString()
        != QLatin1String("driscord")) {
        return { std::nullopt, QStringLiteral("manifest is for another project") };
    }
    const auto channel = object.value(QLatin1String("channel")).toString();
    if (channel != expectedChannel) {
        return { std::nullopt,
            QStringLiteral("manifest channel '%1' does not match '%2'")
                .arg(channel, expectedChannel) };
    }
    const auto target = object.value(QLatin1String("target")).toString();
    if (target != expectedTarget) {
        return { std::nullopt,
            QStringLiteral("manifest target '%1' does not match '%2'")
                .arg(target, expectedTarget) };
    }
    const auto version
        = VersionTriple::parse(object.value(QLatin1String("version")).toString());
    if (!version) {
        return { std::nullopt, QStringLiteral("manifest version is not X.Y.Z") };
    }

    const auto files = object.value(QLatin1String("files")).toArray();
    static const QRegularExpression sha256Pattern(
        QStringLiteral("^[0-9a-f]{64}$"));
    for (const auto value : files) {
        const auto entry = value.toObject();
        UpdateFileEntry file;
        file.name = entry.value(QLatin1String("name")).toString();
        file.sha256 = entry.value(QLatin1String("sha256")).toString();
        file.size = qint64(entry.value(QLatin1String("size")).toDouble());
        file.url = entry.value(QLatin1String("url")).toString();
        if (!file.name.endsWith(archiveSuffix)) {
            continue;
        }
        if (!sha256Pattern.match(file.sha256).hasMatch()) {
            return { std::nullopt,
                QStringLiteral("file '%1' has no valid sha256").arg(file.name) };
        }
        if (file.size <= 0 || file.size > kMaxArchiveBytes) {
            return { std::nullopt,
                QStringLiteral("file '%1' has an implausible size").arg(file.name) };
        }
        if (!isRelativeUrl(file.url)) {
            return { std::nullopt,
                QStringLiteral("file '%1' has a non-relative url").arg(file.name) };
        }
        return { UpdateManifest { *version, channel, target, file }, { } };
    }
    return { std::nullopt,
        QStringLiteral("manifest carries no '%1' archive").arg(archiveSuffix) };
}
