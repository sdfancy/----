#include "diagnostics/EventLog.h"

namespace spray::diagnostics {

void EventLog::append(QString level, QString category, QString message)
{
    records_.append({QDateTime::currentDateTimeUtc(), std::move(level), std::move(category), std::move(message)});
}

QList<EventRecord> EventLog::records() const
{
    return records_;
}

} // namespace spray::diagnostics
