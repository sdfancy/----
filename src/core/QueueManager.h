#pragma once

#include "domain/Snapshots.h"
#include "domain/Types.h"
#include "protocol/PlcProtocol.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

namespace spray::core {

struct ArmQueueItem {
    int armId = 0;
    quint16 pointer = 0;
    quint16 count = 0;
    quint64 enqueueSeq = 0;
    QByteArray payload;
    domain::QueueItemStatus status = domain::QueueItemStatus::Pending;
    domain::PayloadSource source = domain::PayloadSource::Pending;
    int sendCount = 0;
    bool confirmed = false;
};

struct DequeueCache {
    int armId = 0;
    quint16 triggerPointer = 0;
    quint16 selectedPointer = 0;
    int prefetchOffset = 0;
    QByteArray payload;
    quint16 count = 0;
    bool defaultNoop = false;
};

struct RobotTask {
    int armId = 0;
    quint16 count = 0;
    quint16 pointer = 0;
    QByteArray payload;
    bool defaultNoop = false;
};

class QueueManager {
public:
    explicit QueueManager(int prefetchOffset = 0, int maxItemsPerArm = 128);

    void enqueuePair(quint16 count, quint16 pointer);
    bool storeCameraData(int armId, quint16 count, quint16 pointer, const QByteArray& payload);
    QList<RobotTask> handleDequeuePointers(const protocol::PlcDequeueFrame& frame);
    void markTaskAccepted(const RobotTask& task);
    void markTaskDone(const RobotTask& task);

    std::optional<ArmQueueItem> itemFor(int armId, quint16 pointer) const;
    std::optional<DequeueCache> cacheFor(int armId) const;
    domain::QueueSnapshot snapshot() const;

private:
    QList<RobotTask> processArmDequeue(int armId, quint16 triggerPointer);
    std::optional<ArmQueueItem> selectPrefetchItem(int armId, quint16 triggerPointer) const;
    ArmQueueItem makeDefaultItem(int armId, quint16 pointer) const;
    void trimArmQueue(int armId);

    QHash<int, QHash<quint16, ArmQueueItem>> armQueues_;
    QHash<int, quint16> lastDequeuePointers_;
    QHash<int, DequeueCache> caches_;
    quint64 nextSeq_ = 1;
    int prefetchOffset_ = 0;
    int maxItemsPerArm_ = 128;
};

} // namespace spray::core
