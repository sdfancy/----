#include "io/plc/ModbusAddressTable.h"

#include <QFile>
#include <QSet>
#include <QTextStream>

namespace spray::io {

namespace {

QString stripComment(QString line)
{
    const auto hashIndex = line.indexOf('#');
    if (hashIndex >= 0) {
        line.truncate(hashIndex);
    }
    return line.trimmed();
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.mid(1, value.size() - 2);
    }
    return value;
}

bool parseBool(const QString& value, bool fallback)
{
    const auto lowered = value.trimmed().toLower();
    if (lowered == QStringLiteral("true")) {
        return true;
    }
    if (lowered == QStringLiteral("false")) {
        return false;
    }
    return fallback;
}

int parseInt(const QString& value, int fallback)
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

bool parseU16Strict(const QString& value, quint16* result)
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    if (!ok || parsed < 0 || parsed > 65535) {
        return false;
    }
    *result = static_cast<quint16>(parsed);
    return true;
}

bool parseNodeType(const QString& value, ModbusNodeType* type)
{
    const QString lowered = unquote(value).trimmed().toLower();
    if (lowered == QStringLiteral("holding_register")
        || lowered == QStringLiteral("holding_registers")) {
        *type = ModbusNodeType::HoldingRegister;
        return true;
    }
    if (lowered == QStringLiteral("coil") || lowered == QStringLiteral("coils")) {
        *type = ModbusNodeType::Coil;
        return true;
    }
    return false;
}

bool nodeHasContent(const ModbusNode& node)
{
    return !node.name.isEmpty()
        || node.address != 0
        || node.count != 1
        || node.writable
        || !node.description.isEmpty();
}

} // namespace

ModbusAddressTable ModbusAddressTable::load(const QString& path,
                                            QString* errorMessage,
                                            int defaultSlaveId)
{
    ModbusAddressTable table;
    if (path.isEmpty()) {
        return table;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("cannot open modbus address table: %1").arg(path);
        }
        return table;
    }

    ModbusNode current;
    current.slaveId = defaultSlaveId;
    bool inNode = false;

    const auto finishNode = [&]() {
        if (inNode || nodeHasContent(current)) {
            table.addNode(current);
        }
        current = ModbusNode{};
        current.slaveId = defaultSlaveId;
        inNode = false;
    };

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto line = stripComment(stream.readLine());
        if (line.isEmpty()) {
            continue;
        }
        if (line == QStringLiteral("[[nodes]]")) {
            finishNode();
            inNode = true;
            continue;
        }

        const auto equalIndex = line.indexOf('=');
        if (equalIndex < 0) {
            continue;
        }

        const auto key = line.left(equalIndex).trimmed();
        const auto value = line.mid(equalIndex + 1).trimmed();
        if (key == QStringLiteral("name")) {
            current.name = unquote(value);
        } else if (key == QStringLiteral("type")) {
            if (!parseNodeType(value, &current.type)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("invalid modbus node type: %1").arg(unquote(value));
                }
                return {};
            }
        } else if (key == QStringLiteral("slave_id")) {
            current.slaveId = parseInt(value, current.slaveId);
        } else if (key == QStringLiteral("address")) {
            if (!parseU16Strict(value, &current.address)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("invalid modbus node address: %1").arg(unquote(value));
                }
                return {};
            }
        } else if (key == QStringLiteral("count")) {
            if (!parseU16Strict(value, &current.count)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("invalid modbus node count: %1").arg(unquote(value));
                }
                return {};
            }
        } else if (key == QStringLiteral("writable")) {
            current.writable = parseBool(value, current.writable);
        } else if (key == QStringLiteral("description")) {
            current.description = unquote(value);
        }
    }
    finishNode();

    QString validationError;
    if (!table.validate(&validationError)) {
        if (errorMessage) {
            *errorMessage = validationError;
        }
        return {};
    }
    return table;
}

void ModbusAddressTable::addNode(ModbusNode node)
{
    nodes_.append(std::move(node));
}

const ModbusNode* ModbusAddressTable::node(const QString& name) const
{
    for (const auto& item : nodes_) {
        if (item.name == name) {
            return &item;
        }
    }
    return nullptr;
}

QList<ModbusNode> ModbusAddressTable::nodes() const
{
    return nodes_;
}

bool ModbusAddressTable::validate(QString* errorMessage) const
{
    QSet<QString> names;
    for (const auto& item : nodes_) {
        if (item.name.trimmed().isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("modbus node name is empty");
            }
            return false;
        }
        if (names.contains(item.name)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("duplicate modbus node name: %1").arg(item.name);
            }
            return false;
        }
        names.insert(item.name);

        if (item.slaveId <= 0 || item.slaveId > 247 || item.count == 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("modbus node is out of range: %1").arg(item.name);
            }
            return false;
        }
        if (static_cast<int>(item.address) + static_cast<int>(item.count) > 65536) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("modbus node address range is invalid: %1").arg(item.name);
            }
            return false;
        }
        if (item.type == ModbusNodeType::Coil && item.count != 1) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("coil node count must be 1: %1").arg(item.name);
            }
            return false;
        }
    }
    return true;
}

} // namespace spray::io
