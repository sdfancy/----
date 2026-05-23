#include "io/camera/CameraEndpoint.h"

#include <QHostAddress>

#include <utility>

namespace spray::io {
namespace {

QString normalizeCameraKey(const QString& cameraKey)
{
    return cameraKey.trimmed().toLower();
}

} // namespace

CameraEndpoint::CameraEndpoint(config::CameraConfig config, QObject* parent)
    : QObject(parent)
    , config_(std::move(config))
{
    wireServer(camera2dServer_, camera2dSockets_, QStringLiteral("2d"));
    wireServer(camera3dServer_, camera3dSockets_, QStringLiteral("3d"));

    connect(&legacySocket_, &QTcpSocket::readyRead, this, [this]() {
        const QByteArray bytes = legacySocket_.readAll();
        emit rawFrame(QStringLiteral("legacy"), QStringLiteral("rx"), bytes);
        emit payloadReceived(QStringLiteral("legacy"), bytes);
    });
    connect(&legacySocket_, &QTcpSocket::errorOccurred, this, [this]() {
        emit warning(QStringLiteral("legacy camera socket error: %1").arg(legacySocket_.errorString()));
    });
}

bool CameraEndpoint::start(QString* errorMessage)
{
    if (config_.flowMode == config::CameraFlowMode::LegacySingleCamera) {
        return startLegacy(errorMessage);
    }
    return startDual(errorMessage);
}

void CameraEndpoint::stop()
{
    for (QTcpSocket* socket : std::as_const(camera2dSockets_)) {
        socket->disconnectFromHost();
    }
    for (QTcpSocket* socket : std::as_const(camera3dSockets_)) {
        socket->disconnectFromHost();
    }
    camera2dServer_.close();
    camera3dServer_.close();
    legacySocket_.disconnectFromHost();
}

bool CameraEndpoint::sendToCamera(const QString& cameraKey, const QByteArray& payload)
{
    const QString key = normalizeCameraKey(cameraKey);
    if (config_.flowMode == config::CameraFlowMode::LegacySingleCamera) {
        if (key != QStringLiteral("legacy") && key != QStringLiteral("2d")) {
            emit warning(QStringLiteral("legacy camera send got invalid key: %1").arg(cameraKey));
            return false;
        }
        return sendToSocket(&legacySocket_, QStringLiteral("legacy"), payload);
    }

    QList<QTcpSocket*>* sockets = socketsForKey(key);
    if (!sockets) {
        emit warning(QStringLiteral("unknown camera key: %1").arg(cameraKey));
        return false;
    }
    if (sockets->isEmpty()) {
        emit warning(QStringLiteral("camera is not connected: %1").arg(key));
        return false;
    }
    return sendToSocket(sockets->last(), key, payload);
}

int CameraEndpoint::connectionCount(const QString& cameraKey) const
{
    const QString key = normalizeCameraKey(cameraKey);
    if (key == QStringLiteral("legacy")) {
        return legacySocket_.state() == QAbstractSocket::ConnectedState ? 1 : 0;
    }

    const QList<QTcpSocket*>* sockets = socketsForKey(key);
    return sockets ? sockets->size() : 0;
}

quint16 CameraEndpoint::listeningPort(const QString& cameraKey) const
{
    const QString key = normalizeCameraKey(cameraKey);
    if (key == QStringLiteral("2d")) {
        return camera2dServer_.serverPort();
    }
    if (key == QStringLiteral("3d")) {
        return camera3dServer_.serverPort();
    }
    return 0;
}

bool CameraEndpoint::startDual(QString* errorMessage)
{
    if (!listen(camera2dServer_, QStringLiteral("2d"), config_.camera2dPort, errorMessage)) {
        return false;
    }
    if (config_.camera3dEnabled
        && !listen(camera3dServer_, QStringLiteral("3d"), config_.camera3dPort, errorMessage)) {
        camera2dServer_.close();
        return false;
    }
    return true;
}

bool CameraEndpoint::startLegacy(QString* errorMessage)
{
    Q_UNUSED(errorMessage)
    legacySocket_.connectToHost(config_.legacyHost, config_.legacyPort);
    return true;
}

bool CameraEndpoint::listen(
    QTcpServer& server,
    const QString& cameraKey,
    quint16 port,
    QString* errorMessage)
{
    const QHostAddress address(config_.host);
    if (!server.listen(address, port)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 camera listen failed: %2").arg(cameraKey, server.errorString());
        }
        return false;
    }
    return true;
}

void CameraEndpoint::wireServer(QTcpServer& server, QList<QTcpSocket*>& sockets, const QString& cameraKey)
{
    connect(&server, &QTcpServer::newConnection, this, [this, &server, socketsPtr = &sockets, cameraKey]() {
        while (QTcpSocket* socket = server.nextPendingConnection()) {
            socketsPtr->append(socket);
            wireSocket(socket, *socketsPtr, cameraKey);
        }
    });
}

void CameraEndpoint::wireSocket(QTcpSocket* socket, QList<QTcpSocket*>& sockets, const QString& cameraKey)
{
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, cameraKey]() {
        const QByteArray bytes = socket->readAll();
        emit rawFrame(cameraKey, QStringLiteral("rx"), bytes);
        emit payloadReceived(cameraKey, bytes);
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socketsPtr = &sockets, socket]() {
        removeSocket(*socketsPtr, socket);
        socket->deleteLater();
    });
}

void CameraEndpoint::removeSocket(QList<QTcpSocket*>& sockets, QTcpSocket* socket)
{
    sockets.removeAll(socket);
}

QList<QTcpSocket*>* CameraEndpoint::socketsForKey(const QString& cameraKey)
{
    if (cameraKey == QStringLiteral("2d")) {
        return &camera2dSockets_;
    }
    if (cameraKey == QStringLiteral("3d")) {
        return &camera3dSockets_;
    }
    return nullptr;
}

const QList<QTcpSocket*>* CameraEndpoint::socketsForKey(const QString& cameraKey) const
{
    if (cameraKey == QStringLiteral("2d")) {
        return &camera2dSockets_;
    }
    if (cameraKey == QStringLiteral("3d")) {
        return &camera3dSockets_;
    }
    return nullptr;
}

bool CameraEndpoint::sendToSocket(QTcpSocket* socket, const QString& cameraKey, const QByteArray& payload)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        emit warning(QStringLiteral("camera is not connected: %1").arg(cameraKey));
        return false;
    }

    const qint64 written = socket->write(payload);
    socket->flush();
    emit rawFrame(cameraKey, QStringLiteral("tx"), payload);
    return written == payload.size();
}

} // namespace spray::io
