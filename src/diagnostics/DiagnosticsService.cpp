#include "diagnostics/DiagnosticsService.h"

#include "diagnostics/DiagnosticCodes.h"
#include "protocol/ByteCodec.h"

#include <utility>

namespace spray::diagnostics {

DiagnosticsService::DiagnosticsService(config::LoggingConfig config, QObject* parent)
    : QObject(parent)
    , config_(std::move(config))
    , eventLog_(config_.maxInMemoryEvents)
{
    RawFrameFileSinkConfig sinkConfig;
    sinkConfig.logDir = config_.logDir;
    sinkConfig.persistRawFrames = config_.persistRawFrames;
    sinkConfig.persistEvents = config_.persistEvents;
    sinkConfig.flushIntervalMs = config_.flushIntervalMs;
    fileSink_ = std::make_unique<RawFrameFileSink>(sinkConfig, this);
    connect(fileSink_.get(), &RawFrameFileSink::warning, this, [this](const QString& message) {
        recordWarning(
            QStringLiteral("diagnostics"),
            QStringLiteral("diagnostics.log"),
            message,
            DiagnosticCode::logWriteFailed());
    });
}

void DiagnosticsService::recordEvent(DiagnosticEvent event)
{
    eventLog_.append(event);
    if (fileSink_) {
        fileSink_->enqueue(event);
    }
}

void DiagnosticsService::recordMessage(QString level,
                                       QString category,
                                       QString message,
                                       QString device,
                                       QString code,
                                       int count,
                                       int pointer)
{
    recordEvent({
        QDateTime::currentDateTimeUtc(),
        std::move(level),
        std::move(category),
        std::move(device),
        {},
        {},
        count,
        pointer,
        std::move(code),
        std::move(message),
    });
}

void DiagnosticsService::recordRawFrame(QString category,
                                        QString device,
                                        QString direction,
                                        const QByteArray& payload,
                                        int count,
                                        int pointer,
                                        int connectionCount)
{
    health_.recordRawFrame(device, direction, connectionCount);
    if (!config_.rawFrames) {
        return;
    }

    const QString payloadHex = protocol::toHex(payload);
    const QString message = QStringLiteral("%1 %2 %3")
                                .arg(displayDeviceName(device), direction, payloadHex);
    recordEvent({
        QDateTime::currentDateTimeUtc(),
        QStringLiteral("INFO"),
        std::move(category),
        std::move(device),
        std::move(direction),
        payloadHex,
        count,
        pointer,
        {},
        message,
    });
}

void DiagnosticsService::recordWarning(QString category,
                                       QString device,
                                       QString message,
                                       QString code)
{
    health_.recordWarning(device, message);
    recordMessage(
        QStringLiteral("WARN"),
        std::move(category),
        std::move(message),
        std::move(device),
        std::move(code));
}

void DiagnosticsService::updateConnectionCount(const QString& device, int connectionCount)
{
    health_.setConnectionCount(device, connectionCount);
}

QList<EventRecord> DiagnosticsService::events() const
{
    return eventLog_.records();
}

QList<EventRecord> DiagnosticsService::events(const EventFilter& filter) const
{
    return eventLog_.query(filter);
}

QList<domain::DeviceHealthSnapshot> DiagnosticsService::deviceHealthSnapshot() const
{
    return health_.snapshots();
}

domain::DeviceHealthSnapshot DiagnosticsService::deviceHealth(const QString& device) const
{
    return health_.snapshot(device);
}

int DiagnosticsService::droppedCount() const
{
    return eventLog_.droppedCount();
}

bool DiagnosticsService::flush(QString* errorMessage)
{
    if (!fileSink_) {
        return true;
    }
    return fileSink_->flush(errorMessage);
}

QString DiagnosticsService::displayDeviceName(const QString& device) const
{
    const int dotIndex = device.indexOf('.');
    if (dotIndex >= 0 && dotIndex + 1 < device.size()) {
        return device.mid(dotIndex + 1);
    }
    return device;
}

} // namespace spray::diagnostics
