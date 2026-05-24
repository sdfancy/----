#pragma once

#include "io/plc/ModbusTypes.h"

#include <QList>
#include <QString>

namespace spray::io {

class ModbusAddressTable {
public:
    static ModbusAddressTable load(const QString& path,
                                   QString* errorMessage = nullptr,
                                   int defaultSlaveId = 1);

    void addNode(ModbusNode node);
    const ModbusNode* node(const QString& name) const;
    QList<ModbusNode> nodes() const;
    bool validate(QString* errorMessage = nullptr) const;

private:
    QList<ModbusNode> nodes_;
};

} // namespace spray::io
