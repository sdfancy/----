#pragma once

#include <QList>
#include <QString>

#include <utility>

namespace spray::io {

enum class ModbusNodeType {
    HoldingRegister,
    Coil,
};

enum class ModbusErrorCode {
    None,
    Disabled,
    NotConnected,
    InvalidAddress,
    PermissionDenied,
    ProtocolError,
    Timeout,
    TransportError,
    ConfigurationError,
};

struct ModbusError {
    ModbusErrorCode code = ModbusErrorCode::None;
    QString message;
};

struct ModbusNode {
    QString name;
    ModbusNodeType type = ModbusNodeType::HoldingRegister;
    int slaveId = 1;
    quint16 address = 0;
    quint16 count = 1;
    bool writable = false;
    QString description;
};

struct ModbusResult {
    bool ok = false;
    QList<quint16> values;
    ModbusError error;

    static ModbusResult success(QList<quint16> resultValues = {})
    {
        return {true, std::move(resultValues), {}};
    }

    static ModbusResult fail(ModbusErrorCode code, QString message)
    {
        return {false, {}, {code, std::move(message)}};
    }
};

struct ModbusRequestResult {
    bool accepted = false;
    ModbusError error;

    static ModbusRequestResult queued()
    {
        return {true, {}};
    }

    static ModbusRequestResult fail(ModbusErrorCode code, QString message)
    {
        return {false, {code, std::move(message)}};
    }
};

} // namespace spray::io
