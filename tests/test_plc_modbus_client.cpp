#include "test_plc_modbus_client.h"

#include "io/plc/PlcModbusClient.h"

#include <QtTest/QtTest>

using spray::config::ModbusConfig;
using spray::io::IModbusTransport;
using spray::io::ModbusAddressTable;
using spray::io::ModbusCompletion;
using spray::io::ModbusErrorCode;
using spray::io::ModbusNode;
using spray::io::ModbusNodeType;
using spray::io::ModbusRequestResult;
using spray::io::ModbusResult;
using spray::io::PlcModbusClient;

namespace {

class RecordingTransport final : public IModbusTransport {
public:
    bool connected = false;
    ModbusResult nextReadResult = ModbusResult::success({10, 20});
    QStringList calls;

    bool connectToPlc(const ModbusConfig& config, QString* errorMessage) override
    {
        Q_UNUSED(errorMessage);
        calls.append(QStringLiteral("connect:%1:%2").arg(config.host).arg(config.port));
        connected = true;
        return true;
    }

    void disconnectFromPlc() override
    {
        calls.append(QStringLiteral("disconnect"));
        connected = false;
    }

    bool isConnected() const override
    {
        return connected;
    }

    ModbusRequestResult readHoldingRegisters(int slaveId,
                                             quint16 start,
                                             quint16 count,
                                             ModbusCompletion completion) override
    {
        calls.append(QStringLiteral("read_holding:%1:%2:%3").arg(slaveId).arg(start).arg(count));
        if (completion) {
            completion(nextReadResult);
        }
        return ModbusRequestResult::queued();
    }

    ModbusRequestResult writeHoldingRegisters(int slaveId,
                                              quint16 start,
                                              const QList<quint16>& values,
                                              ModbusCompletion completion) override
    {
        calls.append(QStringLiteral("write_holding:%1:%2:%3").arg(slaveId).arg(start).arg(values.size()));
        if (completion) {
            completion(ModbusResult::success());
        }
        return ModbusRequestResult::queued();
    }

    ModbusRequestResult writeCoil(int slaveId,
                                  quint16 address,
                                  bool value,
                                  ModbusCompletion completion) override
    {
        calls.append(QStringLiteral("write_coil:%1:%2:%3").arg(slaveId).arg(address).arg(value));
        if (completion) {
            completion(ModbusResult::success());
        }
        return ModbusRequestResult::queued();
    }
};

ModbusConfig enabledConfig()
{
    ModbusConfig config;
    config.enabled = true;
    config.host = QStringLiteral("192.168.1.20");
    config.port = 502;
    return config;
}

ModbusAddressTable tableWithNodes()
{
    ModbusAddressTable table;
    ModbusNode holding;
    holding.name = QStringLiteral("pressure");
    holding.type = ModbusNodeType::HoldingRegister;
    holding.slaveId = 2;
    holding.address = 400;
    holding.count = 2;
    holding.writable = true;
    table.addNode(holding);

    ModbusNode readOnly;
    readOnly.name = QStringLiteral("read_only");
    readOnly.type = ModbusNodeType::HoldingRegister;
    readOnly.slaveId = 2;
    readOnly.address = 410;
    readOnly.count = 1;
    readOnly.writable = false;
    table.addNode(readOnly);

    ModbusNode coil;
    coil.name = QStringLiteral("manual_enable");
    coil.type = ModbusNodeType::Coil;
    coil.slaveId = 2;
    coil.address = 20;
    coil.count = 1;
    coil.writable = true;
    table.addNode(coil);
    return table;
}

} // namespace

class PlcModbusClientTest final : public QObject {
    Q_OBJECT

private slots:
    void disabledConfigRejectsRequestsWithoutTransportCall()
    {
        auto transport = std::make_unique<RecordingTransport>();
        auto* raw = transport.get();

        PlcModbusClient client(ModbusConfig{}, {}, std::move(transport));
        QString error;
        QVERIFY(!client.connectToPlc(&error));
        QCOMPARE(error, QStringLiteral("modbus is disabled"));

        const auto result = client.readHoldingRegisters(1, 400, 1);
        QVERIFY(!result.accepted);
        QCOMPARE(result.error.code, ModbusErrorCode::Disabled);
        QVERIFY(raw->calls.isEmpty());
    }

    void enabledClientConnectsAndRecordsReadWriteRequests()
    {
        auto transport = std::make_unique<RecordingTransport>();
        auto* raw = transport.get();
        PlcModbusClient client(enabledConfig(), {}, std::move(transport));

        QString error;
        QVERIFY(client.connectToPlc(&error));
        QVERIFY(error.isEmpty());
        QVERIFY(client.isConnected());

        ModbusResult readResult;
        const auto read = client.readHoldingRegisters(1, 400, 2, [&](ModbusResult result) {
            readResult = result;
        });
        QVERIFY(read.accepted);
        QVERIFY(readResult.ok);
        QCOMPARE(readResult.values, QList<quint16>({10, 20}));

        const auto writeRegisters = client.writeHoldingRegisters(1, 410, {123, 456});
        QVERIFY(writeRegisters.accepted);
        const auto writeCoil = client.writeCoil(1, 20, true);
        QVERIFY(writeCoil.accepted);

        QVERIFY(raw->calls.contains(QStringLiteral("connect:192.168.1.20:502")));
        QVERIFY(raw->calls.contains(QStringLiteral("read_holding:1:400:2")));
        QVERIFY(raw->calls.contains(QStringLiteral("write_holding:1:410:2")));
        QVERIFY(raw->calls.contains(QStringLiteral("write_coil:1:20:1")));
    }

    void rejectsRequestsWhenNotConnected()
    {
        auto transport = std::make_unique<RecordingTransport>();
        auto* raw = transport.get();
        PlcModbusClient client(enabledConfig(), {}, std::move(transport));

        const auto result = client.readHoldingRegisters(1, 400, 1);

        QVERIFY(!result.accepted);
        QCOMPARE(result.error.code, ModbusErrorCode::NotConnected);
        QVERIFY(raw->calls.isEmpty());
    }

    void readAndWriteNamedNodes()
    {
        auto transport = std::make_unique<RecordingTransport>();
        auto* raw = transport.get();
        PlcModbusClient client(enabledConfig(), tableWithNodes(), std::move(transport));
        QString error;
        QVERIFY(client.connectToPlc(&error));

        QVERIFY(client.readNode(QStringLiteral("pressure")).accepted);
        QVERIFY(client.writeNode(QStringLiteral("pressure"), {1, 2}).accepted);
        QVERIFY(client.writeNode(QStringLiteral("manual_enable"), {1}).accepted);

        QVERIFY(raw->calls.contains(QStringLiteral("read_holding:2:400:2")));
        QVERIFY(raw->calls.contains(QStringLiteral("write_holding:2:400:2")));
        QVERIFY(raw->calls.contains(QStringLiteral("write_coil:2:20:1")));
    }

    void rejectsReadOnlyNamedWrites()
    {
        auto transport = std::make_unique<RecordingTransport>();
        PlcModbusClient client(enabledConfig(), tableWithNodes(), std::move(transport));
        QString error;
        QVERIFY(client.connectToPlc(&error));

        const auto result = client.writeNode(QStringLiteral("read_only"), {1});

        QVERIFY(!result.accepted);
        QCOMPARE(result.error.code, ModbusErrorCode::PermissionDenied);
    }

    void rejectsInvalidAddressRange()
    {
        auto transport = std::make_unique<RecordingTransport>();
        PlcModbusClient client(enabledConfig(), {}, std::move(transport));
        QString error;
        QVERIFY(client.connectToPlc(&error));

        const auto result = client.readHoldingRegisters(1, 65535, 2);

        QVERIFY(!result.accepted);
        QCOMPARE(result.error.code, ModbusErrorCode::InvalidAddress);
    }

    void emitsWarningForProtocolErrors()
    {
        auto transport = std::make_unique<RecordingTransport>();
        transport->nextReadResult = ModbusResult::fail(
            ModbusErrorCode::ProtocolError,
            QStringLiteral("modbus protocol exception"));
        PlcModbusClient client(enabledConfig(), {}, std::move(transport));
        QSignalSpy warningSpy(&client, &PlcModbusClient::warning);
        QString error;
        QVERIFY(client.connectToPlc(&error));

        const auto result = client.readHoldingRegisters(1, 400, 1);

        QVERIFY(result.accepted);
        QCOMPARE(warningSpy.size(), 1);
        QCOMPARE(warningSpy.takeFirst().at(0).toString(), QStringLiteral("modbus protocol exception"));
    }
};

QObject* createPlcModbusClientTest()
{
    return new PlcModbusClientTest();
}

#include "test_plc_modbus_client.moc"
