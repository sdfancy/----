#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace spray::domain {

struct QueueItemSnapshot {
    int armId = 0;
    quint16 pointer = 0;
    quint16 count = 0;
    QString status;
    QString source;
};

struct CacheSnapshot {
    int armId = 0;
    quint16 triggerPointer = 0;
    quint16 selectedPointer = 0;
    quint16 count = 0;
    bool defaultNoop = false;
};

struct QueueSnapshot {
    QList<QueueItemSnapshot> items;
    QList<CacheSnapshot> caches;
};

struct DeviceHealthSnapshot {
    QString device;
    bool online = false;
    int connectionCount = 0;
    QDateTime lastRxAt;
    QDateTime lastTxAt;
    QDateTime lastErrorAt;
    QString lastError;
};

struct RobotSnapshot {
    int armId = 0;
    bool connected = false;
    bool enabled = false;
    QString mode;
    QString lastError;
};

struct SystemSnapshot {
    QueueSnapshot queue;
    QList<DeviceHealthSnapshot> devices;
    QList<RobotSnapshot> robots;
    QString overallStatus;
    bool isRunning = false;
    QString uptime;
};

} // namespace spray::domain
