#pragma once

// Minimal in-process HTTP/1.1 server over QTcpServer, driven entirely by the
// test's own event loop, so ApiClient/AuthManager are exercised against real
// sockets instead of mocked replies. Responses are scripted per request in
// arrival order; every response carries "Connection: close" so each request
// arrives on its own connection and the script order stays deterministic.
//
// Failure knobs mirror the C++ FakeApiServer conventions: an abrupt close
// without a response and a delayed response (for in-flight cancellation
// races). Anything not scripted is answered 200 {}.

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace test_support {

struct HttpRequest {
    QByteArray method;
    QByteArray path;
    // Keys lower-cased; values as sent.
    QMap<QByteArray, QByteArray> headers;
    QByteArray body;
};

struct HttpResponse {
    int status = 200;
    QByteArray body = "{}";
    QByteArray contentType = "application/json";
    // Record the request, then reset the connection without answering.
    bool closeWithoutResponse = false;
    // Hold the answer back; the request is recorded immediately.
    int delayMs = 0;
};

class HttpFixture : public QObject {
public:
    HttpFixture()
    {
        server_.listen(QHostAddress::LocalHost, 0);
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server_.nextPendingConnection()) {
                socket->setParent(this);
                auto* buffer = new QByteArray;
                connect(socket, &QTcpSocket::readyRead, this,
                    [this, socket, buffer] { onData(socket, buffer); });
                connect(socket, &QObject::destroyed, socket,
                    [buffer] { delete buffer; });
            }
        });
    }

    bool listening() const { return server_.isListening(); }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1")
            .arg(server_.serverPort());
    }

    void enqueue(HttpResponse response) { script_.append(response); }

    const QList<HttpRequest>& requests() const { return requests_; }

private:
    void onData(QTcpSocket* socket, QByteArray* buffer)
    {
        buffer->append(socket->readAll());
        const int headerEnd = buffer->indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }

        HttpRequest request;
        const auto lines = buffer->left(headerEnd).split('\r');
        const auto requestLine = lines.value(0).trimmed().split(' ');
        request.method = requestLine.value(0);
        request.path = requestLine.value(1);
        for (int i = 1; i < lines.size(); ++i) {
            const auto line = lines[i].trimmed();
            const int colon = line.indexOf(':');
            if (colon > 0) {
                request.headers.insert(line.left(colon).toLower(),
                    line.mid(colon + 1).trimmed());
            }
        }

        const int contentLength
            = request.headers.value("content-length", "0").toInt();
        const QByteArray body = buffer->mid(headerEnd + 4);
        if (body.size() < contentLength) {
            return;
        }
        request.body = body.left(contentLength);
        requests_.append(request);

        const HttpResponse response
            = script_.isEmpty() ? HttpResponse { } : script_.takeFirst();
        if (response.closeWithoutResponse) {
            socket->abort();
            return;
        }
        const auto respond = [socket, response] {
            const QByteArray payload = "HTTP/1.1 "
                + QByteArray::number(response.status) + " Status\r\n"
                + "Content-Type: " + response.contentType + "\r\n"
                + "Content-Length: " + QByteArray::number(response.body.size())
                + "\r\nConnection: close\r\n\r\n" + response.body;
            socket->write(payload);
            socket->disconnectFromHost();
        };
        if (response.delayMs > 0) {
            QTimer::singleShot(response.delayMs, socket, respond);
        } else {
            respond();
        }
    }

    QTcpServer server_;
    QList<HttpResponse> script_;
    QList<HttpRequest> requests_;
};

} // namespace test_support
