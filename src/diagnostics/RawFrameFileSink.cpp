#include "diagnostics/RawFrameFileSink.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace spray::diagnostics {

RawFrameFileSink::RawFrameFileSink(RawFrameFileSinkConfig config, QObject* parent)
    : QObject(parent)
    , config_(std::move(config))
{
    if ((config_.persistRawFrames || config_.persistEvents) && config_.flushIntervalMs > 0) {
        connect(&flushTimer_, &QTimer::timeout, this, [this]() {
            QString error;
            if (!flush(&error) && !error.isEmpty()) {
                emit warning(error);
            }
        });
        flushTimer_.start(config_.flushIntervalMs);
    }
}

void RawFrameFileSink::enqueue(const DiagnosticEvent& event)
{
    if (config_.persistEvents) {
        events_.append(event);
    }
    if (config_.persistRawFrames && !event.payloadHex.isEmpty()) {
        rawEvents_.append(event);
    }
}

bool RawFrameFileSink::flush(QString* errorMessage)
{
    if (!QDir().mkpath(config_.logDir)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("cannot create log directory: %1").arg(config_.logDir);
        }
        return false;
    }

    const auto rawCopy = rawEvents_;
    const auto eventCopy = events_;
    if (!appendLines(rawLogPath(), rawCopy, errorMessage)) {
        return false;
    }
    rawEvents_.clear();

    if (!appendLines(eventLogPath(), eventCopy, errorMessage)) {
        return false;
    }
    events_.clear();
    return true;
}

QString RawFrameFileSink::rawLogPath() const
{
    return QDir(config_.logDir).filePath(QStringLiteral("raw-frames.jsonl"));
}

QString RawFrameFileSink::eventLogPath() const
{
    return QDir(config_.logDir).filePath(QStringLiteral("events.jsonl"));
}

bool RawFrameFileSink::hasPendingEvents() const
{
    return !rawEvents_.isEmpty() || !events_.isEmpty();
}

bool RawFrameFileSink::appendLines(const QString& path,
                                   const QList<DiagnosticEvent>& events,
                                   QString* errorMessage)
{
    if (events.isEmpty()) {
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("cannot write diagnostic log: %1").arg(path);
        }
        return false;
    }
    for (const auto& event : events) {
        file.write(toJsonLine(event).toUtf8());
        file.write("\n");
    }
    return true;
}

QString RawFrameFileSink::toJsonLine(const DiagnosticEvent& event) const
{
    QJsonObject object;
    object.insert(QStringLiteral("timestamp"), event.timestamp.toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("level"), event.level);
    object.insert(QStringLiteral("category"), event.category);
    object.insert(QStringLiteral("device"), event.device);
    object.insert(QStringLiteral("direction"), event.direction);
    object.insert(QStringLiteral("payloadHex"), event.payloadHex);
    object.insert(QStringLiteral("count"), event.count);
    object.insert(QStringLiteral("pointer"), event.pointer);
    object.insert(QStringLiteral("code"), event.code);
    object.insert(QStringLiteral("message"), event.message);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace spray::diagnostics
