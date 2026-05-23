#include "core/EnqueueWorkflow.h"

#include "protocol/CameraProtocol.h"

#include <utility>

namespace spray::core {
namespace {

constexpr quint16 kCommandStart = 11;
constexpr quint16 kCommandEnd = 12;

QByteArray commandWithCount(const char* command, quint16 count)
{
    return QByteArray(command) + QByteArray::number(count);
}

QString normalizeCameraKey(const QString& cameraKey)
{
    return cameraKey.trimmed().toLower();
}

} // namespace

EnqueueWorkflow::EnqueueWorkflow(QueueManager* queueManager, QObject* parent)
    : QObject(parent)
    , queueManager_(queueManager)
{
}

void EnqueueWorkflow::setCameraSender(CameraSender sender)
{
    cameraSender_ = std::move(sender);
}

void EnqueueWorkflow::setEnqueueFeedbackSender(EnqueueFeedbackSender sender)
{
    feedbackSender_ = std::move(sender);
}

void EnqueueWorkflow::handlePlcFrame(protocol::PlcEnqueueFrame frame)
{
    if (frame.command == kCommandStart) {
        handleStart(frame.count, frame.pointer);
        return;
    }
    if (frame.command == kCommandEnd) {
        handleEnd(frame.count);
        return;
    }

    emit warning(QStringLiteral("unsupported enqueue workflow command: %1").arg(frame.command));
}

void EnqueueWorkflow::handleCameraPayload(QString cameraKey, QByteArray payload)
{
    const QString key = normalizeCameraKey(cameraKey);
    if (key == QStringLiteral("2d")) {
        buffer2d_.append(payload);
        const auto drained = protocol::drain2dFrames(buffer2d_);
        buffer2d_ = drained.remaining;
        for (const auto& frame : drained.frames) {
            handle2dFrame(frame);
        }
        return;
    }
    if (key == QStringLiteral("3d")) {
        buffer3d_.append(payload);
        const auto drained = protocol::drain3dFrames(buffer3d_);
        buffer3d_ = drained.remaining;
        for (const auto& frame : drained.frames) {
            handle3dFrame(frame);
        }
        return;
    }

    emit warning(QStringLiteral("unknown camera payload key: %1").arg(cameraKey));
}

void EnqueueWorkflow::handleStart(quint16 count, quint16 pointer)
{
    EnqueueCycle cycle;
    cycle.count = count;
    cycle.pointer = pointer;
    cycles_.insert(count, cycle);
    active2dWindowCount_ = count;

    queueManager_->enqueuePair(count, pointer);
    send3dStart(count);
}

void EnqueueWorkflow::handleEnd(quint16 count)
{
    auto it = cycles_.find(count);
    if (it == cycles_.end()) {
        emit warning(QStringLiteral("enqueue end has no active cycle: %1").arg(count));
        return;
    }

    it->endReceived = true;
    if (active2dWindowCount_.has_value() && *active2dWindowCount_ == count) {
        active2dWindowCount_.reset();
    }
    send3dEnd(count);
}

void EnqueueWorkflow::handle2dFrame(const QByteArray& frame)
{
    const auto parsed = protocol::parse2dFrame(frame);
    if (!parsed.hasValue()) {
        emit warning(parsed.error->message);
        return;
    }

    if (parsed.value->kind == protocol::Parsed2dFrameKind::Ready) {
        handle2dReady();
        return;
    }

    sendCamera(QStringLiteral("2d"), QByteArray("OK"));
    handle2dResult(parsed.value->count, parsed.value->partType);
}

void EnqueueWorkflow::handle2dReady()
{
    if (!active2dWindowCount_.has_value()) {
        return;
    }

    auto it = cycles_.find(*active2dWindowCount_);
    if (it == cycles_.end() || it->endReceived) {
        return;
    }
    if (it->requestSentTo2d) {
        return;
    }

    it->readyReceived = true;
    it->requestSentTo2d = true;
    const QByteArray request = QByteArray("(") + QByteArray::number(it->count) + QByteArray(")E");
    if (!sendCamera(QStringLiteral("2d"), request)) {
        it->requestSentTo2d = false;
    }
}

void EnqueueWorkflow::handle2dResult(quint16 count, const QString& partType)
{
    auto it = cycles_.find(count);
    if (it == cycles_.end()) {
        emit warning(QStringLiteral("2D result has no active cycle: %1").arg(count));
        return;
    }
    if (it->endSentTo3d) {
        return;
    }

    it->partType = partType.trimmed().isEmpty() ? QStringLiteral("00,00") : partType.trimmed();
    if (it->endReceived) {
        send3dEnd(count);
    }
}

void EnqueueWorkflow::handle3dFrame(const QByteArray& frame)
{
    const auto segments = protocol::split3dSegments(frame);
    QSet<quint16> touchedCounts;

    for (const auto& segment : segments) {
        const auto parsed = protocol::parse3dSegment(segment);
        if (!parsed.hasValue()) {
            emit warning(parsed.error->message);
            continue;
        }

        auto cycleIt = cycles_.find(parsed.value->count);
        if (cycleIt == cycles_.end()) {
            emit warning(QStringLiteral("3D segment has no active cycle: %1").arg(parsed.value->count));
            continue;
        }

        const bool stored = queueManager_->storeCameraData(
            parsed.value->armId,
            parsed.value->count,
            cycleIt->pointer,
            parsed.value->payload);
        if (!stored) {
            emit warning(QStringLiteral("3D segment did not match queue item: %1").arg(parsed.value->count));
            continue;
        }

        cycleIt->armReady.insert(parsed.value->armId);
        touchedCounts.insert(parsed.value->count);
    }

    for (const quint16 count : touchedCounts) {
        trySendDone(count);
    }
}

void EnqueueWorkflow::send3dStart(quint16 count)
{
    sendCamera(QStringLiteral("3d"), commandWithCount("11,", count));
}

void EnqueueWorkflow::send3dEnd(quint16 count, const QString& partTypeOverride)
{
    auto it = cycles_.find(count);
    if (it == cycles_.end() || it->endSentTo3d) {
        return;
    }

    it->endSentTo3d = true;
    QString partType = partTypeOverride.trimmed();
    if (partType.isEmpty()) {
        partType = it->partType.trimmed();
    }
    if (partType.isEmpty()) {
        partType = QStringLiteral("00,00");
    }

    const QByteArray payload = QByteArray("12,")
        + QByteArray::number(count)
        + QByteArray(",")
        + partType.toLatin1();
    if (!sendCamera(QStringLiteral("3d"), payload)) {
        it->endSentTo3d = false;
    }
}

bool EnqueueWorkflow::sendCamera(const QString& cameraKey, const QByteArray& payload)
{
    if (!cameraSender_) {
        emit warning(QStringLiteral("camera sender is not configured: %1").arg(cameraKey));
        emit cameraCommandSent(cameraKey, payload, false);
        return false;
    }

    const bool ok = cameraSender_(cameraKey, payload);
    emit cameraCommandSent(cameraKey, payload, ok);
    return ok;
}

bool EnqueueWorkflow::sendDone(quint16 count)
{
    Q_UNUSED(count)
    const QByteArray payload("Done");
    if (!feedbackSender_) {
        emit warning(QStringLiteral("enqueue feedback sender is not configured"));
        emit enqueueFeedbackSent(payload, false);
        return false;
    }

    const bool ok = feedbackSender_(payload);
    emit enqueueFeedbackSent(payload, ok);
    return ok;
}

void EnqueueWorkflow::trySendDone(quint16 count)
{
    auto it = cycles_.find(count);
    if (it == cycles_.end() || it->doneSent) {
        return;
    }
    if (!it->armReady.contains(1) || !it->armReady.contains(2)) {
        return;
    }

    it->doneSent = true;
    sendDone(count);
    cycles_.erase(it);
}

} // namespace spray::core
