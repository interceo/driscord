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

#else

namespace utils {

std::optional<std::string> system_ca_bundle_pem()
{
    return std::nullopt;
}

}

#endif
