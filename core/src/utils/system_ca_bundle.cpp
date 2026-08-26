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
            // pem_size excludes the NUL terminator the API still writes.
            bundle.resize(offset + pem_size);
        }
        CertCloseStore(store, 0);

        if (bundle.empty()) {
            return std::nullopt;
        }
        return bundle;
    }

} // namespace

std::optional<std::string> system_ca_bundle_pem()
{
    // The ROOT store changes only through system administration; one snapshot
    // per process is the same trust window a store handle held open would
    // give.
    static const std::optional<std::string> bundle = collect_root_store_pem();
    return bundle;
}

} // namespace utils

#else

namespace utils {

std::optional<std::string> system_ca_bundle_pem()
{
    return std::nullopt;
}

} // namespace utils

#endif
