#include "diagnostics/DeviceSimulator.h"

#include "protocol/ByteCodec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

namespace spray::diagnostics {

bool DeviceSimulator::runPlcMinimalLoop(const config::PlcConfig& plc,
                                        quint16 count,
                                        quint16 pointer,
                                        QByteArray* feedback,
                                        QString* errorMessage,
                                        int timeoutMs)
{
    QTcpSocket enqueueSocket;
    QTcpSocket dequeueSocket;
    const QString host = clientHost(plc.host);
    if (!connectSocket(enqueueSocket, host, plc.enqueuePort, errorMessage, timeoutMs)) {
        return false;
    }
    if (!connectSocket(dequeueSocket, host, plc.dequeuePort, errorMessage, timeoutMs)) {
        return false;
    }
    if (!writeAll(enqueueSocket, enqueueFrame(1, count, pointer), errorMessage, timeoutMs)) {
        return false;
    }
    if (!writeAll(dequeueSocket, dequeueFrame(pointer, 0), errorMessage, timeoutMs)) {
        return false;
    }
    if (!waitForBytes(dequeueSocket, 4, errorMessage, timeoutMs)) {
        return false;
    }
    if (feedback) {
        *feedback = dequeueSocket.read(4);
    }
    return true;
}

bool DeviceSimulator::runDualCameraCycle(const config::PlcConfig& plc,
                                         const config::CameraConfig& camera,
                                         quint16 count,
                                         quint16 pointer,
                                         QByteArray* enqueueFeedback,
                                         QString* errorMessage,
                                         int timeoutMs)
{
    QTcpSocket plcEnqueue;
    QTcpSocket camera2d;
    QTcpSocket camera3d;
    if (!connectSocket(plcEnqueue, clientHost(plc.host), plc.enqueuePort, errorMessage, timeoutMs)) {
        return false;
    }
    if (!connectSocket(camera2d, clientHost(camera.host), camera.camera2dPort, errorMessage, timeoutMs)) {
        return false;
    }
    if (!connectSocket(camera3d, clientHost(camera.host), camera.camera3dPort, errorMessage, timeoutMs)) {
        return false;
    }

    if (!writeAll(plcEnqueue, enqueueFrame(11, count, pointer), errorMessage, timeoutMs)) {
        return false;
    }
    if (!waitForBytes(camera3d, QByteArray("11,").size(), errorMessage, timeoutMs)) {
        return false;
    }
    camera3d.readAll();

    if (!writeAll(camera2d, QByteArray("READY"), errorMessage, timeoutMs)) {
        return false;
    }
    if (!waitForBytes(camera2d, 1, errorMessage, timeoutMs)) {
        return false;
    }
    camera2d.readAll();

    const QByteArray twoDResult = QByteArray::number(count) + QByteArray(",01,02");
    if (!writeAll(camera2d, twoDResult, errorMessage, timeoutMs)) {
        return false;
    }
    if (!waitForBytes(camera2d, 2, errorMessage, timeoutMs)) {
        return false;
    }
    camera2d.readAll();

    if (!writeAll(plcEnqueue, enqueueFrame(12, count, pointer), errorMessage, timeoutMs)) {
        return false;
    }
    if (!waitForBytes(camera3d, QByteArray("12,").size(), errorMessage, timeoutMs)) {
        return false;
    }
    camera3d.readAll();

    const QByteArray payload = QByteArray("(1001,")
        + QByteArray::number(count)
        + QByteArray(",a)EA(2001,")
        + QByteArray::number(count)
        + QByteArray(",b)E");
    if (!writeAll(camera3d, payload, errorMessage, timeoutMs)) {
        return false;
    }
    if (!waitForBytes(plcEnqueue, 4, errorMessage, timeoutMs)) {
        return false;
    }
    if (enqueueFeedback) {
        *enqueueFeedback = plcEnqueue.readAll();
    }
    return true;
}

bool DeviceSimulator::connectSocket(QTcpSocket& socket,
                                    const QString& host,
                                    quint16 port,
                                    QString* errorMessage,
                                    int timeoutMs)
{
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(timeoutMs)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("connect failed %1:%2: %3")
                                .arg(host)
                                .arg(port)
                                .arg(socket.errorString());
        }
        return false;
    }
    return true;
}

bool DeviceSimulator::writeAll(QTcpSocket& socket,
                               const QByteArray& payload,
                               QString* errorMessage,
                               int timeoutMs)
{
    const qint64 written = socket.write(payload);
    if (written != payload.size() || !socket.waitForBytesWritten(timeoutMs)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("write failed: %1").arg(socket.errorString());
        }
        return false;
    }
    return true;
}

bool DeviceSimulator::waitForBytes(QTcpSocket& socket,
                                   qsizetype minBytes,
                                   QString* errorMessage,
                                   int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (socket.bytesAvailable() < minBytes) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (socket.bytesAvailable() >= minBytes) {
            break;
        }
        if (timer.elapsed() >= timeoutMs) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("read timeout: %1").arg(socket.errorString());
            }
            return false;
        }
        QThread::msleep(5);
    }
    return true;
}

QString DeviceSimulator::clientHost(QString host) const
{
    host = host.trimmed();
    if (host.isEmpty() || host == QStringLiteral("0.0.0.0")) {
        return QStringLiteral("127.0.0.1");
    }
    return host;
}

QByteArray DeviceSimulator::enqueueFrame(quint16 command, quint16 count, quint16 pointer) const
{
    return protocol::writeU16BE(command) + protocol::writeU16BE(count) + protocol::writeU16BE(pointer);
}

QByteArray DeviceSimulator::dequeueFrame(quint16 arm1Pointer, quint16 arm2Pointer) const
{
    return protocol::writeU16BE(arm1Pointer) + protocol::writeU16BE(arm2Pointer);
}

} // namespace spray::diagnostics
