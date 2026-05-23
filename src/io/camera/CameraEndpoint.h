#pragma once

#include "config/AppConfig.h"

#include <QByteArray>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace spray::io {

class CameraEndpoint final : public QObject {
    Q_OBJECT

public:
    explicit CameraEndpoint(config::CameraConfig config, QObject* parent = nullptr);

    bool start(QString* errorMessage = nullptr);
    void stop();
    bool sendToCamera(const QString& cameraKey, const QByteArray& payload);
    int connectionCount(const QString& cameraKey) const;
    quint16 listeningPort(const QString& cameraKey) const;

signals:
    void payloadReceived(QString cameraKey, QByteArray payload);
    void rawFrame(QString cameraKey, QString direction, QByteArray payload);
    void warning(QString message);

private:
    bool startDual(QString* errorMessage);
    bool startLegacy(QString* errorMessage);
    bool listen(QTcpServer& server, const QString& cameraKey, quint16 port, QString* errorMessage);
    void wireServer(QTcpServer& server, QList<QTcpSocket*>& sockets, const QString& cameraKey);
    void wireSocket(QTcpSocket* socket, QList<QTcpSocket*>& sockets, const QString& cameraKey);
    void removeSocket(QList<QTcpSocket*>& sockets, QTcpSocket* socket);
    QList<QTcpSocket*>* socketsForKey(const QString& cameraKey);
    const QList<QTcpSocket*>* socketsForKey(const QString& cameraKey) const;
    bool sendToSocket(QTcpSocket* socket, const QString& cameraKey, const QByteArray& payload);

    config::CameraConfig config_;
    QTcpServer camera2dServer_;
    QTcpServer camera3dServer_;
    QList<QTcpSocket*> camera2dSockets_;
    QList<QTcpSocket*> camera3dSockets_;
    QTcpSocket legacySocket_;
};

} // namespace spray::io
