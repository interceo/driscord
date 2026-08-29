#include "Minisign.h"

#include <QCryptographicHash>

#include <openssl/curve25519.h>

namespace minisign {
namespace {

    constexpr int kKeyIdBytes = 8;
    constexpr int kPublicKeyBytes = 32;
    constexpr int kSignatureBytes = 64;

    const QByteArray kSignatureAlgorithm = QByteArrayLiteral("ED");
    const QByteArray kKeyAlgorithm = QByteArrayLiteral("Ed");
    const QByteArray kTrustedCommentPrefix = QByteArrayLiteral("trusted comment: ");

    QByteArray decodeBase64Strict(const QByteArray& line)
    {
        const auto decoded = QByteArray::fromBase64Encoding(line,
            QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        return decoded ? *decoded : QByteArray { };
    }

    bool ed25519Verify(const QByteArray& message, const QByteArray& signature,
        const QByteArray& publicKey)
    {
        return ED25519_verify(
                   reinterpret_cast<const uint8_t*>(message.constData()),
                   size_t(message.size()),
                   reinterpret_cast<const uint8_t*>(signature.constData()),
                   reinterpret_cast<const uint8_t*>(publicKey.constData()))
            == 1;
    }

}

std::optional<PublicKey> parsePublicKey(const QByteArray& text)
{
    QByteArray keyLine;
    for (const auto& line : text.split('\n')) {
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("untrusted comment:")) {
            continue;
        }
        if (!keyLine.isEmpty()) {
            return std::nullopt;
        }
        keyLine = trimmed;
    }
    const auto raw = decodeBase64Strict(keyLine);
    if (raw.size() != 2 + kKeyIdBytes + kPublicKeyBytes
        || !raw.startsWith(kKeyAlgorithm)) {
        return std::nullopt;
    }
    return PublicKey {
        raw.mid(2, kKeyIdBytes),
        raw.mid(2 + kKeyIdBytes),
    };
}

VerifyResult verifyDetached(const QByteArray& content,
    const QByteArray& signatureText, const QList<PublicKey>& trustedKeys)
{
    auto lines = signatureText.split('\n');
    while (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    if (lines.size() != 4 || !lines[2].startsWith(kTrustedCommentPrefix)) {
        return { false, QStringLiteral("malformed signature file"), { } };
    }

    const auto raw = decodeBase64Strict(lines[1]);
    if (raw.size() != 2 + kKeyIdBytes + kSignatureBytes
        || !raw.startsWith(kSignatureAlgorithm)) {
        return { false, QStringLiteral("unsupported signature algorithm"), { } };
    }
    const auto keyId = raw.mid(2, kKeyIdBytes);
    const auto signature = raw.mid(2 + kKeyIdBytes);

    const PublicKey* trusted = nullptr;
    for (const auto& key : trustedKeys) {
        if (key.keyId == keyId && key.key.size() == kPublicKeyBytes) {
            trusted = &key;
            break;
        }
    }
    if (!trusted) {
        return { false,
            QStringLiteral("signature was made with an untrusted key"), { } };
    }

    const auto globalSignature = decodeBase64Strict(lines[3]);
    if (globalSignature.size() != kSignatureBytes) {
        return { false, QStringLiteral("malformed global signature"), { } };
    }
    const auto trustedComment = lines[2].mid(kTrustedCommentPrefix.size());
    if (!ed25519Verify(signature + trustedComment, globalSignature,
            trusted->key)) {
        return { false,
            QStringLiteral("trusted comment fails verification"), { } };
    }

    const auto prehash
        = QCryptographicHash::hash(content, QCryptographicHash::Blake2b_512);
    if (!ed25519Verify(prehash, signature, trusted->key)) {
        return { false, QStringLiteral("content fails verification"), { } };
    }
    return { true, { }, QString::fromUtf8(trustedComment) };
}

}
