#pragma once
#include <QString>

namespace network_proxy {

// Installs a QNetworkProxyFactory that resolves HTTPS_PROXY/HTTP_PROXY/
// ALL_PROXY/NO_PROXY per request, so the REST API, avatars and the update
// channel all follow the same environment as the signaling transport.
//
// Returns the proxy that applies to `probeUrl` for logging, or an empty string
// when nothing in the environment applies to it. A factory is installed either
// way: NO_PROXY can exempt one host without exempting the rest.
QString installFromEnvironment(const QString& probeUrl);

}
