#pragma once

#include "config/AppConfig.h"
#include "diagnostics/DeviceHealthRegistry.h"
#include "diagnostics/EventLog.h"
#include "diagnostics/RawFrameFileSink.h"

#include <memory>

#include <QObject>

namespace spray::diagnostics {

class DiagnosticsService final : public QObject {
    Q_OBJECT

public:
    explicit DiagnosticsService(config::LoggingConfig config, QObject* parent = nullptr);

    void recordEvent(DiagnosticEvent event);
    void recordMessage(QString level,
                       QString category,
                       QString message,
                       QString device = {},
                       QString code = {},
                       int count = -1,
                       int pointer = -1);
    void recordRawFrame(QString category,
                        QString device,
                        QString direction,
                        const QByteArray& payload,
                        int count = -1,
                        int pointer = -1,
                        int connectionCount = -1);
    void recordWarning(QString category,
                       QString device,
                       QString message,
                       QString code = {});
    void updateConnectionCount(const QString& device, int connectionCount);

    QList<EventRecord> events() const;
    QList<EventRecord> events(const EventFilter& filter) const;
    QList<domain::DeviceHealthSnapshot> deviceHealthSnapshot() const;
    domain::DeviceHealthSnapshot deviceHealth(const QString& device) const;
    int droppedCount() const;
    bool flush(QString* errorMessage = nullptr);

private:
    QString displayDeviceName(const QString& device) const;

    config::LoggingConfig config_;
    EventLog eventLog_;
    DeviceHealthRegistry health_;
    std::unique_ptr<RawFrameFileSink> fileSink_;
};

} // namespace spray::diagnostics
