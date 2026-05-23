#pragma once

#include "config/AppConfig.h"
#include "core/QueueManager.h"
#include "protocol/PlcProtocol.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace spray::core {

class LegacyCameraWorkflow final : public QObject {
    Q_OBJECT

public:
    using CameraTriggerSender = std::function<bool(const QByteArray& payload)>;
    using StageFeedbackSender = std::function<bool(const QByteArray& payload)>;

    explicit LegacyCameraWorkflow(QueueManager* queueManager,
                                  config::CameraCorrelationMode correlationMode,
                                  config::CameraCountExtractMode countExtractMode,
                                  QObject* parent = nullptr);

    void setCameraTriggerSender(CameraTriggerSender sender);
    void setStageFeedbackSender(StageFeedbackSender sender);

public slots:
    void handlePlcFrame(protocol::PlcEnqueueFrame frame);
    void handleCameraPayload(QByteArray payload);

signals:
    void cameraTriggerSent(QByteArray payload, bool ok);
    void stageFeedbackSent(QByteArray payload, bool ok);
    void warning(QString message);

private:
    struct WaitingSlot {
        quint16 count = 0;
        quint16 pointer = 0;
    };

    struct LegacyPayload {
        int armId = 0;
        std::optional<quint16> embeddedCount;
    };

    quint16 normalizeCommand(quint16 command) const;
    void sendTrigger(quint16 command, quint16 count);
    bool sendStageFeedback(int armId);
    std::optional<WaitingSlot> resolveSlot(std::optional<quint16> cameraCount) const;
    std::optional<LegacyPayload> parseLegacyPayload(const QByteArray& payload) const;
    std::optional<quint16> extractCameraCount(const QByteArray& payload,
                                              std::optional<quint16> segmentCount) const;
    QByteArray maybeWrapCameraData(const QByteArray& payload,
                                   quint16 slotCount,
                                   std::optional<quint16> cameraCount) const;

    QueueManager* queueManager_ = nullptr;
    config::CameraCorrelationMode correlationMode_ = config::CameraCorrelationMode::Sequential;
    config::CameraCountExtractMode countExtractMode_ = config::CameraCountExtractMode::Auto;
    CameraTriggerSender triggerSender_;
    StageFeedbackSender feedbackSender_;
    std::optional<WaitingSlot> currentWaitingSlot_;
    std::optional<quint16> currentCycleCount_;
    QHash<quint16, quint16> pointerByCount_;
};

} // namespace spray::core
