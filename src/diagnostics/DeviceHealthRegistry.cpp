#include "diagnostics/DeviceHealthRegistry.h"

namespace spray::diagnostics {

void DeviceHealthRegistry::setConnectionCount(const QString& device, int connectionCount)
{
    auto& item = ensure(device);
    item.connectionCount = connectionCount;
    item.online = connectionCount > 0;
}

void DeviceHealthRegistry::setOnline(const QString& device, bool online)
{
    ensure(device).online = online;
}

void DeviceHealthRegistry::recordRawFrame(const QString& device, const QString& direction, int connectionCount)
{
    auto& item = ensure(device);
    const auto now = QDateTime::currentDateTimeUtc();
    if (direction == QStringLiteral("rx")) {
        item.lastRxAt = now;
    } else if (direction == QStringLiteral("tx")) {
        item.lastTxAt = now;
    }
    if (connectionCount >= 0) {
        item.connectionCount = connectionCount;
    }
    item.online = item.connectionCount > 0 || item.online;
}

void DeviceHealthRegistry::recordWarning(const QString& device, const QString& message)
{
    auto& item = ensure(device);
    item.lastErrorAt = QDateTime::currentDateTimeUtc();
    item.lastError = message;
}

domain::DeviceHealthSnapshot DeviceHealthRegistry::snapshot(const QString& device) const
{
    return devices_.value(device);
}

QList<domain::DeviceHealthSnapshot> DeviceHealthRegistry::snapshots() const
{
    return devices_.values();
}

domain::DeviceHealthSnapshot& DeviceHealthRegistry::ensure(const QString& device)
{
    auto& item = devices_[device];
    if (item.device.isEmpty()) {
        item.device = device;
    }
    return item;
}

} // namespace spray::diagnostics
