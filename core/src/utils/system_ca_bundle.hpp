#pragma once

#include <optional>
#include <string>

namespace utils {

// PEM bundle of the operating system's trusted root certificates, or nullopt
// on platforms whose TLS backend already reads the system trust store itself.
// libdatachannel's MbedTLS backend (the Windows client) verifies wss peers
// only against an explicitly supplied CA chain, so the Windows implementation
// serialises the ROOT certificate store; GnuTLS and OpenSSL consult the
// system store natively and get nullopt.
std::optional<std::string> system_ca_bundle_pem();

} // namespace utils
