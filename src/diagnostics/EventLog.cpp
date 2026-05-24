#include "diagnostics/EventLog.h"

namespace spray::diagnostics {

bool EventFilter::matches(const DiagnosticEvent& event) const
{
    if (!level.isEmpty() && event.level != level) {
        return false;
    }
    if (!category.isEmpty() && event.category != category) {
        return false;
    }
    if (!device.isEmpty() && event.device != device) {
        return false;
    }
    if (count >= 0 && event.count != count) {
        return false;
    }
    if (pointer >= 0 && event.pointer != pointer) {
        return false;
    }
    if (from.isValid() && event.timestamp < from) {
        return false;
    }
    if (to.isValid() && event.timestamp > to) {
        return false;
    }
    return true;
}

EventLog::EventLog(int maxEvents)
    : maxEvents_(maxEvents > 0 ? maxEvents : 1000)
{
}

void EventLog::append(QString level, QString category, QString message)
{
    append(DiagnosticEvent{
        QDateTime::currentDateTimeUtc(),
        std::move(level),
        std::move(category),
        {},
        {},
        {},
        -1,
        -1,
        {},
        std::move(message),
    });
}

void EventLog::append(DiagnosticEvent event)
{
    if (!event.timestamp.isValid()) {
        event.timestamp = QDateTime::currentDateTimeUtc();
    }
    records_.append(std::move(event));
    trimToCapacity();
}

QList<EventRecord> EventLog::records() const
{
    return records_;
}

QList<EventRecord> EventLog::query(const EventFilter& filter) const
{
    QList<EventRecord> out;
    for (const auto& event : records_) {
        if (filter.matches(event)) {
            out.append(event);
        }
    }
    return out;
}

void EventLog::clear()
{
    records_.clear();
    droppedCount_ = 0;
}

void EventLog::setMaxEvents(int maxEvents)
{
    if (maxEvents <= 0) {
        return;
    }
    maxEvents_ = maxEvents;
    trimToCapacity();
}

int EventLog::maxEvents() const
{
    return maxEvents_;
}

int EventLog::droppedCount() const
{
    return droppedCount_;
}

void EventLog::trimToCapacity()
{
    while (records_.size() > maxEvents_) {
        records_.removeFirst();
        ++droppedCount_;
    }
}

} // namespace spray::diagnostics
