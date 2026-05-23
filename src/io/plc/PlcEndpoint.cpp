#include "io/plc/PlcEndpoint.h"

#include <QHostAddress>

namespace spray::io {

PlcEndpoint::PlcEndpoint(config::PlcConfig config, QObject* parent)
    : QObject(parent)
    , config_(std::move(config))
{
    wireServer(enqueueServer_, enqueueSockets_, QStringLiteral("enqueue"));
    wireServer(dequeueServer_, dequeueSockets_, QStringLiteral("dequeue"));
}

bool PlcEndpoint::start(QString* errorMessage)
{
    const QHostAddress address(config_.host);
    if (!enqueueServer_.listen(address, config_.enqueuePort)) {
        if (errorMessage) {
            *errorMessage = enqueueServer_.errorString();
        }
        return false;
    }
    if (!dequeueServer_.listen(address, config_.dequeuePort)) {
        if (errorMessage) {
            *errorMessage = dequeueServer_.errorString();
        }
        enqueueServer_.close();
        return false;
    }
    return true;
}

void PlcEndpoint::stop()
{
    for (QTcpSocket* socket : std::as_const(enqueueSockets_)) {
        socket->disconnectFromHost();
    }
    for (QTcpSocket* socket : std::as_const(dequeueSockets_)) {
        socket->disconnectFromHost();
    }
    enqueueServer_.close();
    dequeueServer_.close();
}

bool PlcEndpoint::sendDequeueFeedback(const QByteArray& bytes)
{
    return sendFeedback(
        dequeueSockets_,
        QStringLiteral("dequeue"),
        bytes,
        QStringLiteral("dequeue feedback has no active 9090 connection"));
}

bool PlcEndpoint::sendEnqueueFeedback(const QByteArray& bytes)
{
    return sendFeedback(
        enqueueSockets_,
        QStringLiteral("enqueue"),
        bytes,
        QStringLiteral("enqueue feedback has no active 9999 connection"));
}

int PlcEndpoint::enqueueConnectionCount() const
{
    return enqueueSockets_.size();
}

int PlcEndpoint::dequeueConnectionCount() const
{
    return dequeueSockets_.size();
}

void PlcEndpoint::wireServer(QTcpServer& server, QList<QTcpSocket*>& sockets, const QString& channel)
{
    connect(&server, &QTcpServer::newConnection, this, [this, &server, socketsPtr = &sockets, channel]() {
        while (QTcpSocket* socket = server.nextPendingConnection()) {
            socketsPtr->append(socket);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, channel]() {
                const QByteArray bytes = socket->readAll();
                emit rawFrame(channel, QStringLiteral("rx"), bytes);
                if (channel == QStringLiteral("enqueue")) {
                    emit enqueueFrameReceived(bytes);
                } else {
                    emit dequeueFrameReceived(bytes);
                }
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socketsPtr, socket]() {
                removeSocket(*socketsPtr, socket);
                socket->deleteLater();
            });
        }
    });
}

void PlcEndpoint::removeSocket(QList<QTcpSocket*>& sockets, QTcpSocket* socket)
{
    sockets.removeAll(socket);
}

bool PlcEndpoint::sendFeedback(QList<QTcpSocket*>& sockets,
                               const QString& channel,
                               const QByteArray& bytes,
                               const QString& missingConnectionMessage)
{
    if (sockets.isEmpty()) {
        emit warning(missingConnectionMessage);
        return false;
    }

    QTcpSocket* socket = sockets.last();
    const qint64 written = socket->write(bytes);
    socket->flush();
    emit rawFrame(channel, QStringLiteral("tx"), bytes);
    return written == bytes.size();
}

} // namespace spray::io
