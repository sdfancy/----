#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace spray::diagnostics {

struct DiagnosticEvent {
    QDateTime timestamp;
    QString level;
    QString category;
    QString device;
    QString direction;
    QString payloadHex;
    int count = -1;
    int pointer = -1;
    QString code;
    QString message;
};

using EventRecord = DiagnosticEvent;

struct EventFilter {
    QString level;
    QString category;
    QString device;
    int count = -1;
    int pointer = -1;
    QDateTime from;
    QDateTime to;

    bool matches(const DiagnosticEvent& event) const;
};

class EventLog {
public:
    explicit EventLog(int maxEvents = 1000);

    void append(QString level, QString category, QString message);
    void append(DiagnosticEvent event);
    QList<EventRecord> records() const;
    QList<EventRecord> query(const EventFilter& filter) const;
    void clear();
    void setMaxEvents(int maxEvents);
    int maxEvents() const;
    int droppedCount() const;

private:
    void trimToCapacity();

    QList<EventRecord> records_;
    int maxEvents_ = 1000;
    int droppedCount_ = 0;
};

} // namespace spray::diagnostics
