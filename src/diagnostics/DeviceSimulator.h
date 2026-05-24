#pragma once

#include "config/AppConfig.h"

#include <QByteArray>
#include <QString>
#include <QTcpSocket>

namespace spray::diagnostics {

class DeviceSimulator {
public:
    bool runPlcMinimalLoop(const config::PlcConfig& plc,
                           quint16 count,
                           quint16 pointer,
                           QByteArray* feedback,
                           QString* errorMessage = nullptr,
                           int timeoutMs = 1000);

    bool runDualCameraCycle(const config::PlcConfig& plc,
                            const config::CameraConfig& camera,
                            quint16 count,
                            quint16 pointer,
                            QByteArray* enqueueFeedback,
                            QString* errorMessage = nullptr,
                            int timeoutMs = 1000);

private:
    bool connectSocket(QTcpSocket& socket, const QString& host, quint16 port, QString* errorMessage, int timeoutMs);
    bool writeAll(QTcpSocket& socket, const QByteArray& payload, QString* errorMessage, int timeoutMs);
    bool waitForBytes(QTcpSocket& socket, qsizetype minBytes, QString* errorMessage, int timeoutMs);
    QString clientHost(QString host) const;
    QByteArray enqueueFrame(quint16 command, quint16 count, quint16 pointer) const;
    QByteArray dequeueFrame(quint16 arm1Pointer, quint16 arm2Pointer) const;
};

} // namespace spray::diagnostics
