#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

namespace minisign {

struct PublicKey {
    QByteArray keyId;
    QByteArray key;
};

std::optional<PublicKey> parsePublicKey(const QByteArray& text);

struct VerifyResult {
    bool ok = false;
    QString error;
    QString trustedComment;
};

VerifyResult verifyDetached(const QByteArray& content,
    const QByteArray& signatureText, const QList<PublicKey>& trustedKeys);

}
