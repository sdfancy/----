#pragma once

#include "domain/Snapshots.h"

#include <QMap>
#include <QString>

namespace spray::diagnostics {

class DeviceHealthRegistry {
public:
    void setConnectionCount(const QString& device, int connectionCount);
    void setOnline(const QString& device, bool online);
    void recordRawFrame(const QString& device, const QString& direction, int connectionCount = -1);
    void recordWarning(const QString& device, const QString& message);

    domain::DeviceHealthSnapshot snapshot(const QString& device) const;
    QList<domain::DeviceHealthSnapshot> snapshots() const;

private:
    domain::DeviceHealthSnapshot& ensure(const QString& device);

    QMap<QString, domain::DeviceHealthSnapshot> devices_;
};

} // namespace spray::diagnostics
