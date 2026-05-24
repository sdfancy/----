#include "test_diagnostics_event_log.h"

#include "diagnostics/DiagnosticCodes.h"
#include "diagnostics/EventLog.h"

#include <QtTest/QtTest>

using spray::diagnostics::DiagnosticEvent;
using spray::diagnostics::EventFilter;
using spray::diagnostics::EventLog;
namespace DiagnosticCode = spray::diagnostics::DiagnosticCode;

class DiagnosticsEventLogTest final : public QObject {
    Q_OBJECT

private slots:
    void appendsAndQueriesStructuredEvents()
    {
        EventLog log;
        log.append(DiagnosticEvent{
            {},
            QStringLiteral("INFO"),
            QStringLiteral("plc.raw"),
            QStringLiteral("plc.enqueue"),
            QStringLiteral("rx"),
            QStringLiteral("00 01 00 0A 00 01"),
            10,
            1,
            {},
            QStringLiteral("enqueue rx"),
        });
        log.append(DiagnosticEvent{
            {},
            QStringLiteral("WARN"),
            QStringLiteral("plc.enqueue"),
            QStringLiteral("plc.enqueue"),
            {},
            {},
            -1,
            -1,
            DiagnosticCode::plcProtocolInvalidFrame(),
            QStringLiteral("bad frame"),
        });

        EventFilter filter;
        filter.device = QStringLiteral("plc.enqueue");
        filter.count = 10;
        const auto events = log.query(filter);

        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().category, QStringLiteral("plc.raw"));
        QCOMPARE(events.first().direction, QStringLiteral("rx"));
        QCOMPARE(events.first().payloadHex, QStringLiteral("00 01 00 0A 00 01"));
        QCOMPARE(events.first().pointer, 1);
    }

    void keepsLegacyAppendCompatibility()
    {
        EventLog log;
        log.append(QStringLiteral("INFO"), QStringLiteral("app"), QStringLiteral("started"));

        const auto records = log.records();

        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().level, QStringLiteral("INFO"));
        QCOMPARE(records.first().category, QStringLiteral("app"));
        QCOMPARE(records.first().message, QStringLiteral("started"));
        QVERIFY(records.first().timestamp.isValid());
    }

    void appliesCapacityLimitAndDroppedCount()
    {
        EventLog log(2);
        log.append(QStringLiteral("INFO"), QStringLiteral("test"), QStringLiteral("one"));
        log.append(QStringLiteral("INFO"), QStringLiteral("test"), QStringLiteral("two"));
        log.append(QStringLiteral("INFO"), QStringLiteral("test"), QStringLiteral("three"));

        const auto records = log.records();

        QCOMPARE(records.size(), 2);
        QCOMPARE(records.first().message, QStringLiteral("two"));
        QCOMPARE(records.last().message, QStringLiteral("three"));
        QCOMPARE(log.droppedCount(), 1);
    }

    void clearRemovesRecordsAndDroppedCount()
    {
        EventLog log(1);
        log.append(QStringLiteral("INFO"), QStringLiteral("test"), QStringLiteral("one"));
        log.append(QStringLiteral("INFO"), QStringLiteral("test"), QStringLiteral("two"));
        QVERIFY(log.droppedCount() > 0);

        log.clear();

        QVERIFY(log.records().isEmpty());
        QCOMPARE(log.droppedCount(), 0);
    }
};

QObject* createDiagnosticsEventLogTest()
{
    return new DiagnosticsEventLogTest();
}

#include "test_diagnostics_event_log.moc"
