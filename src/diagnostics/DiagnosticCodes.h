#pragma once

#include <QString>

namespace spray::diagnostics::DiagnosticCode {

inline QString plcProtocolInvalidFrame()
{
    return QStringLiteral("PLC_PROTOCOL_INVALID_FRAME");
}

inline QString cameraWarning()
{
    return QStringLiteral("CAMERA_WARNING");
}

inline QString robotFault()
{
    return QStringLiteral("ROBOT_FAULT");
}

inline QString modbusError()
{
    return QStringLiteral("MODBUS_ERROR");
}

inline QString logWriteFailed()
{
    return QStringLiteral("LOG_WRITE_FAILED");
}

inline QString eventDropped()
{
    return QStringLiteral("EVENT_DROPPED");
}

} // namespace spray::diagnostics::DiagnosticCode
