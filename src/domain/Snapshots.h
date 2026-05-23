#pragma once

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

} // namespace spray::domain
