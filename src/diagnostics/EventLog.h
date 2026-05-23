#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace spray::diagnostics {

struct EventRecord {
    QDateTime timestamp;
    QString level;
    QString category;
    QString message;
};

class EventLog {
public:
    void append(QString level, QString category, QString message);
    QList<EventRecord> records() const;

private:
    QList<EventRecord> records_;
};

} // namespace spray::diagnostics
