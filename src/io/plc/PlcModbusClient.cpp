#include "io/plc/PlcModbusClient.h"

#if SPRAY_ENABLE_MODBUS
#include <QModbusDataUnit>
#include <QModbusDevice>
#include <QModbusReply>
#include <QModbusTcpClient>
#endif

#include <QPointer>

namespace spray::io {

namespace {

#if SPRAY_ENABLE_MODBUS

ModbusErrorCode mapQtError(QModbusDevice::Error error)
{
    switch (error) {
    case QModbusDevice::NoError:
        return ModbusErrorCode::None;
    case QModbusDevice::TimeoutError:
        return ModbusErrorCode::Timeout;
    case QModbusDevice::ProtocolError:
        return ModbusErrorCode::ProtocolError;
    case QModbusDevice::ConnectionError:
        return ModbusErrorCode::NotConnected;
    case QModbusDevice::ConfigurationError:
        return ModbusErrorCode::ConfigurationError;
    default:
        return ModbusErrorCode::TransportError;
    }
}

ModbusResult resultFromReply(QModbusReply* reply)
{
    const auto error = reply->error();
    if (error == QModbusDevice::NoError) {
        QList<quint16> values;
        const auto unit = reply->result();
        for (uint i = 0; i < unit.valueCount(); ++i) {
            values.append(unit.value(static_cast<int>(i)));
        }
        return ModbusResult::success(values);
    }
    return ModbusResult::fail(mapQtError(error), reply->errorString());
}

QModbusDataUnit holdingUnit(quint16 start, quint16 count)
{
    return {QModbusDataUnit::HoldingRegisters, start, count};
}

QModbusDataUnit holdingWriteUnit(quint16 start, const QList<quint16>& values)
{
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, start, values.size());
    for (qsizetype i = 0; i < values.size(); ++i) {
        unit.setValue(i, values.at(i));
    }
    return unit;
}

QModbusDataUnit coilWriteUnit(quint16 address, bool value)
{
    QModbusDataUnit unit(QModbusDataUnit::Coils, address, 1);
    unit.setValue(0, value ? 1 : 0);
    return unit;
}

class QtModbusTransport final : public IModbusTransport {
public:
    bool connectToPlc(const config::ModbusConfig& config, QString* errorMessage) override
    {
        device_.setConnectionParameter(QModbusDevice::NetworkAddressParameter, config.host);
        device_.setConnectionParameter(QModbusDevice::NetworkPortParameter, config.port);
        device_.setTimeout(config.timeoutMs);
        device_.setNumberOfRetries(config.retries);
        if (!device_.connectDevice()) {
            if (errorMessage) {
                *errorMessage = device_.errorString();
            }
            return false;
        }
        return true;
    }

    void disconnectFromPlc() override
    {
        device_.disconnectDevice();
    }

    bool isConnected() const override
    {
        return device_.state() == QModbusDevice::ConnectedState;
    }

    ModbusRequestResult readHoldingRegisters(int slaveId,
                                             quint16 start,
                                             quint16 count,
                                             ModbusCompletion completion) override
    {
        return sendRequest(device_.sendReadRequest(holdingUnit(start, count), slaveId), std::move(completion));
    }

    ModbusRequestResult writeHoldingRegisters(int slaveId,
                                              quint16 start,
                                              const QList<quint16>& values,
                                              ModbusCompletion completion) override
    {
        return sendRequest(device_.sendWriteRequest(holdingWriteUnit(start, values), slaveId), std::move(completion));
    }

    ModbusRequestResult writeCoil(int slaveId,
                                  quint16 address,
                                  bool value,
                                  ModbusCompletion completion) override
    {
        return sendRequest(device_.sendWriteRequest(coilWriteUnit(address, value), slaveId), std::move(completion));
    }

private:
    ModbusRequestResult sendRequest(QModbusReply* reply, ModbusCompletion completion)
    {
        if (!reply) {
            return ModbusRequestResult::fail(ModbusErrorCode::TransportError, device_.errorString());
        }
        if (reply->isFinished()) {
            if (completion) {
                completion(resultFromReply(reply));
            }
            reply->deleteLater();
            return ModbusRequestResult::queued();
        }

        QObject::connect(reply, &QModbusReply::finished, reply, [reply, completion = std::move(completion)]() {
            if (completion) {
                completion(resultFromReply(reply));
            }
            reply->deleteLater();
        });
        return ModbusRequestResult::queued();
    }

    QModbusTcpClient device_;
};

#else

class UnavailableModbusTransport final : public IModbusTransport {
public:
    bool connectToPlc(const config::ModbusConfig& config, QString* errorMessage) override
    {
        Q_UNUSED(config);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Qt SerialBus support is not enabled");
        }
        return false;
    }

    void disconnectFromPlc() override {}

    bool isConnected() const override
    {
        return false;
    }

    ModbusRequestResult readHoldingRegisters(int slaveId,
                                             quint16 start,
                                             quint16 count,
                                             ModbusCompletion completion) override
    {
        Q_UNUSED(slaveId);
        Q_UNUSED(start);
        Q_UNUSED(count);
        Q_UNUSED(completion);
        return unavailable();
    }

    ModbusRequestResult writeHoldingRegisters(int slaveId,
                                              quint16 start,
                                              const QList<quint16>& values,
                                              ModbusCompletion completion) override
    {
        Q_UNUSED(slaveId);
        Q_UNUSED(start);
        Q_UNUSED(values);
        Q_UNUSED(completion);
        return unavailable();
    }

    ModbusRequestResult writeCoil(int slaveId,
                                  quint16 address,
                                  bool value,
                                  ModbusCompletion completion) override
    {
        Q_UNUSED(slaveId);
        Q_UNUSED(address);
        Q_UNUSED(value);
        Q_UNUSED(completion);
        return unavailable();
    }

private:
    static ModbusRequestResult unavailable()
    {
        return ModbusRequestResult::fail(
            ModbusErrorCode::TransportError,
            QStringLiteral("Qt SerialBus support is not enabled"));
    }
};

#endif

std::unique_ptr<IModbusTransport> createDefaultTransport()
{
#if SPRAY_ENABLE_MODBUS
    return std::make_unique<QtModbusTransport>();
#else
    return std::make_unique<UnavailableModbusTransport>();
#endif
}

ModbusRequestResult permissionDenied(const QString& name)
{
    return ModbusRequestResult::fail(
        ModbusErrorCode::PermissionDenied,
        QStringLiteral("modbus node is not writable: %1").arg(name));
}

} // namespace

PlcModbusClient::PlcModbusClient(config::ModbusConfig config,
                                 ModbusAddressTable addressTable,
                                 std::unique_ptr<IModbusTransport> transport,
                                 QObject* parent)
    : QObject(parent)
    , config_(std::move(config))
    , addressTable_(std::move(addressTable))
    , transport_(std::move(transport))
{
    if (!transport_) {
        transport_ = createDefaultTransport();
    }
}

bool PlcModbusClient::connectToPlc(QString* errorMessage)
{
    if (!config_.enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("modbus is disabled");
        }
        emit warning(QStringLiteral("modbus is disabled"));
        return false;
    }
    const bool ok = transport_->connectToPlc(config_, errorMessage);
    if (!ok) {
        emit warning(errorMessage && !errorMessage->isEmpty()
                         ? *errorMessage
                         : QStringLiteral("modbus connect failed"));
    }
    return ok;
}

void PlcModbusClient::disconnectFromPlc()
{
    if (transport_) {
        transport_->disconnectFromPlc();
    }
}

bool PlcModbusClient::isEnabled() const
{
    return config_.enabled;
}

bool PlcModbusClient::isConnected() const
{
    return config_.enabled && transport_ && transport_->isConnected();
}

ModbusRequestResult PlcModbusClient::readHoldingRegisters(int slaveId,
                                                          quint16 start,
                                                          quint16 count,
                                                          ModbusCompletion completion)
{
    const auto guard = guardRequest(slaveId, start, count);
    if (!guard.accepted) {
        return guard;
    }
    return transport_->readHoldingRegisters(slaveId, start, count, wrapCompletion(std::move(completion)));
}

ModbusRequestResult PlcModbusClient::writeHoldingRegisters(int slaveId,
                                                           quint16 start,
                                                           const QList<quint16>& values,
                                                           ModbusCompletion completion)
{
    if (values.isEmpty()) {
        return warnAndFail(
            ModbusErrorCode::InvalidAddress,
            QStringLiteral("modbus write values are empty"));
    }
    const auto guard = guardRequest(slaveId, start, static_cast<quint16>(values.size()));
    if (!guard.accepted) {
        return guard;
    }
    return transport_->writeHoldingRegisters(slaveId, start, values, wrapCompletion(std::move(completion)));
}

ModbusRequestResult PlcModbusClient::writeCoil(int slaveId,
                                               quint16 address,
                                               bool value,
                                               ModbusCompletion completion)
{
    const auto guard = guardRequest(slaveId, address, 1);
    if (!guard.accepted) {
        return guard;
    }
    return transport_->writeCoil(slaveId, address, value, wrapCompletion(std::move(completion)));
}

ModbusRequestResult PlcModbusClient::readNode(const QString& name, ModbusCompletion completion)
{
    const auto* item = addressTable_.node(name);
    if (!item) {
        return failForMissingNode(name);
    }
    if (item->type != ModbusNodeType::HoldingRegister) {
        return warnAndFail(
            ModbusErrorCode::ConfigurationError,
            QStringLiteral("readNode supports holding registers only: %1").arg(name));
    }
    return readHoldingRegisters(item->slaveId, item->address, item->count, std::move(completion));
}

ModbusRequestResult PlcModbusClient::writeNode(const QString& name,
                                               const QList<quint16>& values,
                                               ModbusCompletion completion)
{
    const auto* item = addressTable_.node(name);
    if (!item) {
        return failForMissingNode(name);
    }
    if (!item->writable) {
        return permissionDenied(name);
    }
    if (item->type == ModbusNodeType::Coil) {
        if (values.size() != 1) {
            return warnAndFail(
                ModbusErrorCode::InvalidAddress,
                QStringLiteral("coil node requires one value: %1").arg(name));
        }
        return writeCoil(item->slaveId, item->address, values.first() != 0, std::move(completion));
    }
    if (values.size() != item->count) {
        return warnAndFail(
            ModbusErrorCode::InvalidAddress,
            QStringLiteral("holding register value count does not match node: %1").arg(name));
    }
    return writeHoldingRegisters(item->slaveId, item->address, values, std::move(completion));
}

ModbusRequestResult PlcModbusClient::guardRequest(int slaveId, quint16 start, quint16 count)
{
    if (!config_.enabled) {
        return warnAndFail(ModbusErrorCode::Disabled, QStringLiteral("modbus is disabled"));
    }
    if (slaveId <= 0 || slaveId > 247 || count == 0
        || static_cast<int>(start) + static_cast<int>(count) > 65536) {
        return warnAndFail(ModbusErrorCode::InvalidAddress, QStringLiteral("modbus address is invalid"));
    }
    if (!transport_ || !transport_->isConnected()) {
        return warnAndFail(ModbusErrorCode::NotConnected, QStringLiteral("modbus is not connected"));
    }
    return ModbusRequestResult::queued();
}

ModbusRequestResult PlcModbusClient::failForMissingNode(const QString& name)
{
    return warnAndFail(
        ModbusErrorCode::ConfigurationError,
        QStringLiteral("modbus node not found: %1").arg(name));
}

ModbusRequestResult PlcModbusClient::warnAndFail(ModbusErrorCode code, const QString& message)
{
    emit warning(message);
    return ModbusRequestResult::fail(code, message);
}

ModbusCompletion PlcModbusClient::wrapCompletion(ModbusCompletion completion)
{
    QPointer<PlcModbusClient> self(this);
    return [self, completion = std::move(completion)](ModbusResult result) mutable {
        if (self && !result.ok && !result.error.message.isEmpty()) {
            emit self->warning(result.error.message);
        }
        if (completion) {
            completion(std::move(result));
        }
    };
}

} // namespace spray::io
