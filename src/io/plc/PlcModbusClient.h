#pragma once

#include "config/AppConfig.h"
#include "io/plc/ModbusAddressTable.h"
#include "io/plc/ModbusTypes.h"

#include <QObject>

#include <functional>
#include <memory>

namespace spray::io {

using ModbusCompletion = std::function<void(ModbusResult)>;

class IModbusTransport {
public:
    virtual ~IModbusTransport() = default;

    virtual bool connectToPlc(const config::ModbusConfig& config, QString* errorMessage) = 0;
    virtual void disconnectFromPlc() = 0;
    virtual bool isConnected() const = 0;
    virtual ModbusRequestResult readHoldingRegisters(int slaveId,
                                                     quint16 start,
                                                     quint16 count,
                                                     ModbusCompletion completion) = 0;
    virtual ModbusRequestResult writeHoldingRegisters(int slaveId,
                                                      quint16 start,
                                                      const QList<quint16>& values,
                                                      ModbusCompletion completion) = 0;
    virtual ModbusRequestResult writeCoil(int slaveId,
                                          quint16 address,
                                          bool value,
                                          ModbusCompletion completion) = 0;
};

class PlcModbusClient final : public QObject {
    Q_OBJECT

public:
    explicit PlcModbusClient(config::ModbusConfig config,
                             ModbusAddressTable addressTable = {},
                             std::unique_ptr<IModbusTransport> transport = nullptr,
                             QObject* parent = nullptr);

    bool connectToPlc(QString* errorMessage = nullptr);
    void disconnectFromPlc();
    bool isEnabled() const;
    bool isConnected() const;

    ModbusRequestResult readHoldingRegisters(int slaveId,
                                             quint16 start,
                                             quint16 count,
                                             ModbusCompletion completion = {});
    ModbusRequestResult writeHoldingRegisters(int slaveId,
                                              quint16 start,
                                              const QList<quint16>& values,
                                              ModbusCompletion completion = {});
    ModbusRequestResult writeCoil(int slaveId,
                                  quint16 address,
                                  bool value,
                                  ModbusCompletion completion = {});
    ModbusRequestResult readNode(const QString& name, ModbusCompletion completion = {});
    ModbusRequestResult writeNode(const QString& name,
                                  const QList<quint16>& values,
                                  ModbusCompletion completion = {});

signals:
    void warning(QString message);

private:
    ModbusRequestResult guardRequest(int slaveId, quint16 start, quint16 count);
    ModbusRequestResult failForMissingNode(const QString& name);
    ModbusRequestResult warnAndFail(ModbusErrorCode code, const QString& message);
    ModbusCompletion wrapCompletion(ModbusCompletion completion);

    config::ModbusConfig config_;
    ModbusAddressTable addressTable_;
    std::unique_ptr<IModbusTransport> transport_;
};

} // namespace spray::io
