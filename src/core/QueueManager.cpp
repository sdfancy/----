#include "core/QueueManager.h"

#include "domain/Payloads.h"

#include <algorithm>

namespace spray::core {

namespace {

QString statusName(domain::QueueItemStatus status)
{
    switch (status) {
    case domain::QueueItemStatus::Pending:
        return QStringLiteral("pending");
    case domain::QueueItemStatus::Cached:
        return QStringLiteral("cached");
    case domain::QueueItemStatus::Accepted:
        return QStringLiteral("accepted");
    case domain::QueueItemStatus::Done:
        return QStringLiteral("done");
    }
    return QStringLiteral("unknown");
}

QString sourceName(domain::PayloadSource source)
{
    switch (source) {
    case domain::PayloadSource::Pending:
        return QStringLiteral("pending");
    case domain::PayloadSource::Camera:
        return QStringLiteral("camera");
    case domain::PayloadSource::Default:
        return QStringLiteral("default");
    }
    return QStringLiteral("unknown");
}

} // namespace

QueueManager::QueueManager(int prefetchOffset, int maxItemsPerArm)
    : prefetchOffset_(std::max(0, prefetchOffset))
    , maxItemsPerArm_(std::max(1, maxItemsPerArm))
{
    lastDequeuePointers_.insert(1, 0);
    lastDequeuePointers_.insert(2, 0);
}

void QueueManager::enqueuePair(quint16 count, quint16 pointer)
{
    for (const int armId : {1, 2}) {
        ArmQueueItem item;
        item.armId = armId;
        item.pointer = pointer;
        item.count = count;
        item.enqueueSeq = nextSeq_++;
        item.payload = domain::formatDefaultPayload(armId, count);
        item.source = domain::PayloadSource::Default;

        armQueues_[armId].insert(pointer, item);
        trimArmQueue(armId);
    }
}

bool QueueManager::storeCameraData(int armId, quint16 count, quint16 pointer, const QByteArray& payload)
{
    auto& armQueue = armQueues_[armId];
    auto it = armQueue.find(pointer);
    if (it == armQueue.end() || it->count != count) {
        return false;
    }

    it->payload = payload;
    it->source = domain::PayloadSource::Camera;
    return true;
}

QList<RobotTask> QueueManager::handleDequeuePointers(const protocol::PlcDequeueFrame& frame)
{
    QList<RobotTask> tasks;
    const QList<QPair<int, quint16>> pointers{
        {1, frame.arm1Pointer},
        {2, frame.arm2Pointer},
    };

    for (const auto& [armId, pointer] : pointers) {
        if (pointer == 0) {
            continue;
        }
        if (pointer == lastDequeuePointers_.value(armId, 0)) {
            continue;
        }

        lastDequeuePointers_[armId] = pointer;
        tasks.append(processArmDequeue(armId, pointer));
    }

    return tasks;
}

void QueueManager::markTaskAccepted(const RobotTask& task)
{
    auto it = armQueues_[task.armId].find(task.pointer);
    if (it != armQueues_[task.armId].end()) {
        it->status = domain::QueueItemStatus::Accepted;
        it->sendCount += 1;
    }
}

void QueueManager::markTaskDone(const RobotTask& task)
{
    auto it = armQueues_[task.armId].find(task.pointer);
    if (it != armQueues_[task.armId].end()) {
        it->status = domain::QueueItemStatus::Done;
        it->confirmed = true;
    }
    caches_.remove(task.armId);
}

std::optional<ArmQueueItem> QueueManager::itemFor(int armId, quint16 pointer) const
{
    const auto armIt = armQueues_.find(armId);
    if (armIt == armQueues_.end()) {
        return std::nullopt;
    }
    const auto itemIt = armIt->find(pointer);
    if (itemIt == armIt->end()) {
        return std::nullopt;
    }
    return *itemIt;
}

std::optional<DequeueCache> QueueManager::cacheFor(int armId) const
{
    const auto it = caches_.find(armId);
    if (it == caches_.end()) {
        return std::nullopt;
    }
    return *it;
}

domain::QueueSnapshot QueueManager::snapshot() const
{
    domain::QueueSnapshot out;
    for (auto armIt = armQueues_.cbegin(); armIt != armQueues_.cend(); ++armIt) {
        for (auto itemIt = armIt->cbegin(); itemIt != armIt->cend(); ++itemIt) {
            out.items.append({
                itemIt->armId,
                itemIt->pointer,
                itemIt->count,
                statusName(itemIt->status),
                sourceName(itemIt->source),
            });
        }
    }
    for (auto it = caches_.cbegin(); it != caches_.cend(); ++it) {
        out.caches.append({
            it->armId,
            it->triggerPointer,
            it->selectedPointer,
            it->count,
            it->defaultNoop,
        });
    }
    return out;
}

QList<RobotTask> QueueManager::processArmDequeue(int armId, quint16 triggerPointer)
{
    QList<RobotTask> tasks;
    const auto selected = selectPrefetchItem(armId, triggerPointer).value_or(makeDefaultItem(armId, triggerPointer));

    DequeueCache cache;
    cache.armId = armId;
    cache.triggerPointer = triggerPointer;
    cache.selectedPointer = selected.pointer;
    cache.prefetchOffset = prefetchOffset_;
    cache.payload = selected.payload;
    cache.count = selected.count;
    cache.defaultNoop = selected.source == domain::PayloadSource::Default;
    caches_[armId] = cache;

    auto it = armQueues_[armId].find(selected.pointer);
    if (it != armQueues_[armId].end()) {
        it->status = domain::QueueItemStatus::Cached;
    }

    tasks.append({armId, selected.count, selected.pointer, selected.payload, cache.defaultNoop});
    return tasks;
}

std::optional<ArmQueueItem> QueueManager::selectPrefetchItem(int armId, quint16 triggerPointer) const
{
    const auto armIt = armQueues_.find(armId);
    if (armIt == armQueues_.end()) {
        return std::nullopt;
    }

    const auto selectedPointer = static_cast<quint16>(triggerPointer + prefetchOffset_);
    const auto itemIt = armIt->find(selectedPointer);
    if (itemIt == armIt->end()) {
        return std::nullopt;
    }
    return *itemIt;
}

ArmQueueItem QueueManager::makeDefaultItem(int armId, quint16 pointer) const
{
    return {
        armId,
        pointer,
        0,
        0,
        domain::formatDefaultPayload(armId, 0),
        domain::QueueItemStatus::Cached,
        domain::PayloadSource::Default,
        0,
        false,
    };
}

void QueueManager::trimArmQueue(int armId)
{
    auto& queue = armQueues_[armId];
    while (queue.size() > maxItemsPerArm_) {
        auto oldest = queue.begin();
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->enqueueSeq < oldest->enqueueSeq) {
                oldest = it;
            }
        }
        queue.erase(oldest);
    }
}

} // namespace spray::core
