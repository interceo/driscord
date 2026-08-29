#pragma once

#include <QByteArray>
#include <QCryptographicHash>

#include <openssl/curve25519.h>

namespace test_support {

struct MinisignKeyPair {
    QByteArray keyId;
    QByteArray publicKey;
    QByteArray privateKey;

    static MinisignKeyPair generate(const QByteArray& keyId
        = QByteArrayLiteral("\x01\x02\x03\x04\x05\x06\x07\x08"))
    {
        MinisignKeyPair pair;
        pair.keyId = keyId;
        uint8_t publicKey[32];
        uint8_t privateKey[64];
        ED25519_keypair(publicKey, privateKey);
        pair.publicKey
            = QByteArray(reinterpret_cast<const char*>(publicKey), 32);
        pair.privateKey
            = QByteArray(reinterpret_cast<const char*>(privateKey), 64);
        return pair;
    }

    QByteArray publicKeyLine() const
    {
        return (QByteArrayLiteral("Ed") + keyId + publicKey).toBase64();
    }

    QByteArray signContent(const QByteArray& content,
        const QByteArray& trustedComment
        = QByteArrayLiteral("timestamp:0\tfile:latest.json")) const
    {
        const auto prehash = QCryptographicHash::hash(
            content, QCryptographicHash::Blake2b_512);
        uint8_t signature[64];
        ED25519_sign(signature,
            reinterpret_cast<const uint8_t*>(prehash.constData()),
            size_t(prehash.size()),
            reinterpret_cast<const uint8_t*>(privateKey.constData()));
        const auto signatureBytes
            = QByteArray(reinterpret_cast<const char*>(signature), 64);

        const QByteArray globalMessage = signatureBytes + trustedComment;
        uint8_t globalSignature[64];
        ED25519_sign(globalSignature,
            reinterpret_cast<const uint8_t*>(globalMessage.constData()),
            size_t(globalMessage.size()),
            reinterpret_cast<const uint8_t*>(privateKey.constData()));

        return QByteArrayLiteral("untrusted comment: test signature\n")
            + (QByteArrayLiteral("ED") + keyId + signatureBytes).toBase64()
            + '\n' + QByteArrayLiteral("trusted comment: ") + trustedComment
            + '\n'
            + QByteArray(reinterpret_cast<const char*>(globalSignature), 64)
                  .toBase64()
            + '\n';
    }
};

}
