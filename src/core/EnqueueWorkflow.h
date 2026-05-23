#pragma once

#include "core/QueueManager.h"
#include "protocol/PlcProtocol.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

namespace spray::core {

class EnqueueWorkflow final : public QObject {
    Q_OBJECT

public:
    using CameraSender = std::function<bool(const QString& cameraKey, const QByteArray& payload)>;
    using EnqueueFeedbackSender = std::function<bool(const QByteArray& payload)>;

    explicit EnqueueWorkflow(QueueManager* queueManager, QObject* parent = nullptr);

    void setCameraSender(CameraSender sender);
    void setEnqueueFeedbackSender(EnqueueFeedbackSender sender);

public slots:
    void handlePlcFrame(protocol::PlcEnqueueFrame frame);
    void handleCameraPayload(QString cameraKey, QByteArray payload);

signals:
    void cameraCommandSent(QString cameraKey, QByteArray payload, bool ok);
    void enqueueFeedbackSent(QByteArray payload, bool ok);
    void warning(QString message);

private:
    struct EnqueueCycle {
        quint16 count = 0;
        quint16 pointer = 0;
        bool readyReceived = false;
        bool requestSentTo2d = false;
        bool endReceived = false;
        bool endSentTo3d = false;
        bool doneSent = false;
        QString partType;
        QSet<int> armReady;
    };

    void handleStart(quint16 count, quint16 pointer);
    void handleEnd(quint16 count);
    void handle2dFrame(const QByteArray& frame);
    void handle2dReady();
    void handle2dResult(quint16 count, const QString& partType);
    void handle3dFrame(const QByteArray& frame);
    void send3dStart(quint16 count);
    void send3dEnd(quint16 count, const QString& partTypeOverride = {});
    bool sendCamera(const QString& cameraKey, const QByteArray& payload);
    bool sendDone(quint16 count);
    void trySendDone(quint16 count);

    QueueManager* queueManager_ = nullptr;
    CameraSender cameraSender_;
    EnqueueFeedbackSender feedbackSender_;
    QHash<quint16, EnqueueCycle> cycles_;
    std::optional<quint16> active2dWindowCount_;
    QByteArray buffer2d_;
    QByteArray buffer3d_;
};

} // namespace spray::core
