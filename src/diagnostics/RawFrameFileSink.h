#pragma once

#include "diagnostics/EventLog.h"

#include <QObject>
#include <QTimer>

namespace spray::diagnostics {

struct RawFrameFileSinkConfig {
    QString logDir = QStringLiteral("logs");
    bool persistRawFrames = false;
    bool persistEvents = false;
    int flushIntervalMs = 1000;
};

class RawFrameFileSink final : public QObject {
    Q_OBJECT

public:
    explicit RawFrameFileSink(RawFrameFileSinkConfig config, QObject* parent = nullptr);

    void enqueue(const DiagnosticEvent& event);
    bool flush(QString* errorMessage = nullptr);
    QString rawLogPath() const;
    QString eventLogPath() const;
    bool hasPendingEvents() const;

signals:
    void warning(QString message);

private:
    bool appendLines(const QString& path, const QList<DiagnosticEvent>& events, QString* errorMessage);
    QString toJsonLine(const DiagnosticEvent& event) const;

    RawFrameFileSinkConfig config_;
    QList<DiagnosticEvent> rawEvents_;
    QList<DiagnosticEvent> events_;
    QTimer flushTimer_;
};

} // namespace spray::diagnostics
