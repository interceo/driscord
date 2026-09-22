#include "utils/system_ca_bundle.hpp"

#ifdef _WIN32

#include <mutex>

#include <windows.h>

#include <wincrypt.h>

namespace utils {
namespace {

    std::optional<std::string> collect_root_store_pem()
    {
        HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
        if (store == nullptr) {
            return std::nullopt;
        }

        std::string bundle;
        PCCERT_CONTEXT context = nullptr;
        while ((context = CertEnumCertificatesInStore(store, context)) != nullptr) {
            DWORD pem_size = 0;
            if (CryptBinaryToStringA(context->pbCertEncoded,
                    context->cbCertEncoded, CRYPT_STRING_BASE64HEADER, nullptr,
                    &pem_size)
                == FALSE) {
                continue;
            }
            const std::size_t offset = bundle.size();
            bundle.resize(offset + pem_size);
            if (CryptBinaryToStringA(context->pbCertEncoded,
                    context->cbCertEncoded, CRYPT_STRING_BASE64HEADER,
                    bundle.data() + offset, &pem_size)
                == FALSE) {
                bundle.resize(offset);
                continue;
            }
            bundle.resize(offset + pem_size);
        }
        CertCloseStore(store, 0);

        if (bundle.empty()) {
            return std::nullopt;
        }
        return bundle;
    }

}

std::optional<std::string> system_ca_bundle_pem()
{
    static const std::optional<std::string> bundle = collect_root_store_pem();
    return bundle;
}

}

#elif defined(__APPLE__)

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace utils {
namespace {

    constexpr std::string_view kBase64Alphabet
        = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    constexpr std::size_t kPemLineLength = 64;

    void append_base64_lines(std::string& out, const std::uint8_t* data,
        std::size_t size)
    {
        std::size_t column = 0;
        const auto put = [&](char c) {
            out.push_back(c);
            if (++column == kPemLineLength) {
                out.push_back('\n');
                column = 0;
            }
        };
        for (std::size_t i = 0; i < size; i += 3) {
            const std::size_t remaining = size - i;
            const std::uint32_t group = (std::uint32_t { data[i] } << 16)
                | (remaining > 1 ? std::uint32_t { data[i + 1] } << 8 : 0u)
                | (remaining > 2 ? std::uint32_t { data[i + 2] } : 0u);
            put(kBase64Alphabet[(group >> 18) & 0x3f]);
            put(kBase64Alphabet[(group >> 12) & 0x3f]);
            put(remaining > 1 ? kBase64Alphabet[(group >> 6) & 0x3f] : '=');
            put(remaining > 2 ? kBase64Alphabet[group & 0x3f] : '=');
        }
        if (column != 0) {
            out.push_back('\n');
        }
    }

    std::optional<std::string> collect_root_store_pem()
    {
        CFArrayRef anchors = nullptr;
        if (SecTrustCopyAnchorCertificates(&anchors) != errSecSuccess
            || anchors == nullptr) {
            return std::nullopt;
        }

        std::string bundle;
        const CFIndex count = CFArrayGetCount(anchors);
        for (CFIndex index = 0; index < count; ++index) {
            auto certificate = static_cast<SecCertificateRef>(
                const_cast<void*>(CFArrayGetValueAtIndex(anchors, index)));
            CFDataRef der = SecCertificateCopyData(certificate);
            if (der == nullptr) {
                continue;
            }
            bundle += "-----BEGIN CERTIFICATE-----\n";
            append_base64_lines(bundle, CFDataGetBytePtr(der),
                static_cast<std::size_t>(CFDataGetLength(der)));
            bundle += "-----END CERTIFICATE-----\n";
            CFRelease(der);
        }
        CFRelease(anchors);

        if (bundle.empty()) {
            return std::nullopt;
        }
        return bundle;
    }

}

std::optional<std::string> system_ca_bundle_pem()
{
    static const std::optional<std::string> bundle = collect_root_store_pem();
    return bundle;
}

}

#else

namespace utils {

std::optional<std::string> system_ca_bundle_pem()
{
    return std::nullopt;
}

}

#endif
