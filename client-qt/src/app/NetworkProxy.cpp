#include "NetworkProxy.h"

#include "proxy_config.hpp"

#include <QNetworkProxy>
#include <QUrl>

namespace network_proxy {
namespace {

    QNetworkProxy toQtProxy(const utils::ProxyConfig& proxy)
    {
        QNetworkProxy result(proxy.scheme == utils::ProxyConfig::Scheme::Socks5
                ? QNetworkProxy::Socks5Proxy
                : QNetworkProxy::HttpProxy,
            QString::fromStdString(proxy.host), proxy.port);
        if (proxy.authenticated()) {
            result.setUser(QString::fromStdString(proxy.username));
            result.setPassword(QString::fromStdString(proxy.password));
        }
        return result;
    }

    // Qt asks per request, which is what makes NO_PROXY work for one host
    // while the rest still goes through the proxy.
    class EnvironmentProxyFactory final : public QNetworkProxyFactory {
    public:
        QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override
        {
            QString scheme = query.url().scheme();
            if (scheme.isEmpty()) {
                scheme = query.protocolTag();
            }
            const auto proxy = utils::proxy_from_environment(
                scheme.toStdString(), query.peerHostName().toStdString());
            if (!proxy) {
                return { QNetworkProxy(QNetworkProxy::NoProxy) };
            }
            return { toQtProxy(*proxy) };
        }
    };

}

QString installFromEnvironment(const QString& probeUrl)
{
    QNetworkProxyFactory::setApplicationProxyFactory(new EnvironmentProxyFactory);

    const auto proxy = utils::proxy_for_url(probeUrl.toStdString());
    return proxy ? QString::fromStdString(proxy->url()) : QString { };
}

}
