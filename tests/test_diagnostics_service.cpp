#include "test_diagnostics_service.h"

#include "diagnostics/DiagnosticCodes.h"
#include "diagnostics/DiagnosticsService.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using spray::config::LoggingConfig;
using spray::diagnostics::DiagnosticsService;
using spray::diagnostics::EventFilter;
namespace DiagnosticCode = spray::diagnostics::DiagnosticCode;

class DiagnosticsServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void rawFramesUpdateEventsAndHealth()
    {
        LoggingConfig config;
        DiagnosticsService diagnostics(config);

        diagnostics.recordRawFrame(
            QStringLiteral("plc.raw"),
            QStringLiteral("plc.enqueue"),
            QStringLiteral("rx"),
            QByteArray::fromHex("0001000A0001"),
            10,
            1,
            1);

        EventFilter filter;
        filter.device = QStringLiteral("plc.enqueue");
        filter.count = 10;
        const auto events = diagnostics.events(filter);
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().payloadHex, QStringLiteral("00 01 00 0A 00 01"));
        QCOMPARE(events.first().pointer, 1);

        const auto health = diagnostics.deviceHealth(QStringLiteral("plc.enqueue"));
        QVERIFY(health.online);
        QCOMPARE(health.connectionCount, 1);
        QVERIFY(health.lastRxAt.isValid());
    }

    void warningsUpdateEventsAndHealth()
    {
        DiagnosticsService diagnostics(LoggingConfig{});

        diagnostics.recordWarning(
            QStringLiteral("robot"),
            QStringLiteral("robot"),
            QStringLiteral("DUCO open failed"),
            DiagnosticCode::robotFault());

        const auto events = diagnostics.events();
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().level, QStringLiteral("WARN"));
        QCOMPARE(events.first().code, DiagnosticCode::robotFault());

        const auto health = diagnostics.deviceHealth(QStringLiteral("robot"));
        QVERIFY(health.lastErrorAt.isValid());
        QCOMPARE(health.lastError, QStringLiteral("DUCO open failed"));
    }

    void writesConfiguredLogsOnFlush()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        LoggingConfig config;
        config.logDir = dir.path();
        config.persistRawFrames = true;
        config.persistEvents = true;
        DiagnosticsService diagnostics(config);

        diagnostics.recordRawFrame(
            QStringLiteral("plc.raw"),
            QStringLiteral("plc.enqueue"),
            QStringLiteral("rx"),
            QByteArray::fromHex("0001000A0001"),
            10,
            1,
            1);
        diagnostics.recordMessage(
            QStringLiteral("INFO"),
            QStringLiteral("queue.enqueue"),
            QStringLiteral("count=10 pointer=1"),
            QStringLiteral("queue"),
            {},
            10,
            1);

        QString error;
        QVERIFY2(diagnostics.flush(&error), qPrintable(error));

        QFile rawFile(dir.filePath(QStringLiteral("raw-frames.jsonl")));
        QVERIFY(rawFile.exists());
        QVERIFY(rawFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(rawFile.readAll()).contains(QStringLiteral("00 01 00 0A 00 01")));

        QFile eventFile(dir.filePath(QStringLiteral("events.jsonl")));
        QVERIFY(eventFile.exists());
        QVERIFY(eventFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString eventText = QString::fromUtf8(eventFile.readAll());
        QVERIFY(eventText.contains(QStringLiteral("queue.enqueue")));
        QVERIFY(eventText.contains(QStringLiteral("\"count\":10")));
    }

    void disabledRawPersistenceDoesNotWriteRawFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        LoggingConfig config;
        config.logDir = dir.path();
        config.persistRawFrames = false;
        config.persistEvents = false;
        DiagnosticsService diagnostics(config);
        diagnostics.recordRawFrame(
            QStringLiteral("plc.raw"),
            QStringLiteral("plc.enqueue"),
            QStringLiteral("rx"),
            QByteArray::fromHex("0001000A0001"));

        QString error;
        QVERIFY2(diagnostics.flush(&error), qPrintable(error));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("raw-frames.jsonl"))));
    }
};

QObject* createDiagnosticsServiceTest()
{
    return new DiagnosticsServiceTest();
}

#include "test_diagnostics_service.moc"
