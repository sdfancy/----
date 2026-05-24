#include "test_modbus_address_table.h"

#include "io/plc/ModbusAddressTable.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using spray::io::ModbusAddressTable;
using spray::io::ModbusNodeType;

namespace {

QString writeTable(QTemporaryDir& dir, const QByteArray& bytes)
{
    const QString path = dir.filePath(QStringLiteral("modbus_nodes.toml"));
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Text);
    Q_ASSERT(opened);
    if (!opened) {
        return path;
    }
    file.write(bytes);
    file.close();
    return path;
}

} // namespace

class ModbusAddressTableTest final : public QObject {
    Q_OBJECT

private slots:
    void allowsEmptyAddressTable()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString error;
        const auto table = ModbusAddressTable::load(writeTable(dir, QByteArray()), &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(table.nodes().isEmpty());
    }

    void loadsNamedNodes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString error;
        const auto table = ModbusAddressTable::load(writeTable(dir, R"toml(
[[nodes]]
name = "spray_ready"
type = "holding_register"
slave_id = 2
address = 400
count = 2
writable = false
description = "ready values"

[[nodes]]
name = "manual_enable"
type = "coil"
slave_id = 2
address = 20
writable = true
)toml"), &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(table.nodes().size(), 2);

        const auto* holding = table.node(QStringLiteral("spray_ready"));
        QVERIFY(holding != nullptr);
        QCOMPARE(holding->type, ModbusNodeType::HoldingRegister);
        QCOMPARE(holding->slaveId, 2);
        QCOMPARE(holding->address, 400);
        QCOMPARE(holding->count, 2);
        QVERIFY(!holding->writable);

        const auto* coil = table.node(QStringLiteral("manual_enable"));
        QVERIFY(coil != nullptr);
        QCOMPARE(coil->type, ModbusNodeType::Coil);
        QCOMPARE(coil->address, 20);
        QVERIFY(coil->writable);
    }

    void rejectsDuplicateName()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString error;
        const auto table = ModbusAddressTable::load(writeTable(dir, R"toml(
[[nodes]]
name = "same"
type = "holding_register"
address = 1

[[nodes]]
name = "same"
type = "holding_register"
address = 2
)toml"), &error);

        Q_UNUSED(table);
        QVERIFY(error.contains(QStringLiteral("duplicate")));
    }

    void rejectsInvalidAddressRange()
    {
        ModbusAddressTable table;
        spray::io::ModbusNode node;
        node.name = QStringLiteral("bad");
        node.address = 65535;
        node.count = 2;
        table.addNode(node);

        QString error;
        QVERIFY(!table.validate(&error));
        QVERIFY(error.contains(QStringLiteral("address range")));
    }

    void rejectsNegativeAddressFromFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString error;
        const auto table = ModbusAddressTable::load(writeTable(dir, R"toml(
[[nodes]]
name = "negative"
type = "holding_register"
address = -1
)toml"), &error);

        QVERIFY(table.nodes().isEmpty());
        QVERIFY(error.contains(QStringLiteral("invalid modbus node address")));
    }

    void rejectsNonSingleCoilNode()
    {
        ModbusAddressTable table;
        spray::io::ModbusNode node;
        node.name = QStringLiteral("bad_coil");
        node.type = ModbusNodeType::Coil;
        node.count = 2;
        table.addNode(node);

        QString error;
        QVERIFY(!table.validate(&error));
        QVERIFY(error.contains(QStringLiteral("coil node count")));
    }
};

QObject* createModbusAddressTableTest()
{
    return new ModbusAddressTableTest();
}

#include "test_modbus_address_table.moc"
