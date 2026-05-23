#include "core/LegacyCameraWorkflow.h"

#include "protocol/ByteCodec.h"

#include <QRegularExpression>
#include <QStringList>

#include <utility>

namespace spray::core {
namespace {

constexpr quint16 kLegacyCommandTrigger = 1;
constexpr quint16 kLegacyCommandArm2 = 11;
constexpr quint16 kLegacyCommandEnd = 12;

std::optional<quint16> parseU16Text(const QString& text)
{
    bool ok = false;
    const uint value = text.trimmed().toUInt(&ok);
    if (!ok || value > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<quint16>(value);
}

bool isMostlyPrintableText(const QByteArray& payload)
{
    if (payload.isEmpty()) {
        return false;
    }

    int printable = 0;
    for (const char byte : payload) {
        const auto value = static_cast<unsigned char>(byte);
        if ((value >= 32 && value <= 126) || value == 9 || value == 10 || value == 13) {
            ++printable;
        }
    }
    return static_cast<double>(printable) / static_cast<double>(payload.size()) >= 0.85;
}

std::optional<quint16> extractAsciiCount(const QByteArray& payload)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\b(?:COUNT|CNT)\\s*[:=]\\s*(\\d+)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(QString::fromUtf8(payload));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return parseU16Text(match.captured(1));
}

std::optional<quint16> extractBinaryCount(const QByteArray& payload)
{
    if (payload.size() < 4) {
        return std::nullopt;
    }
    return protocol::readU16BE(payload, 2);
}

} // namespace

LegacyCameraWorkflow::LegacyCameraWorkflow(QueueManager* queueManager,
                                           config::CameraCorrelationMode correlationMode,
                                           config::CameraCountExtractMode countExtractMode,
                                           QObject* parent)
    : QObject(parent)
    , queueManager_(queueManager)
    , correlationMode_(correlationMode)
    , countExtractMode_(countExtractMode)
{
}

void LegacyCameraWorkflow::setCameraTriggerSender(CameraTriggerSender sender)
{
    triggerSender_ = std::move(sender);
}

void LegacyCameraWorkflow::setStageFeedbackSender(StageFeedbackSender sender)
{
    feedbackSender_ = std::move(sender);
}

void LegacyCameraWorkflow::handlePlcFrame(protocol::PlcEnqueueFrame frame)
{
    const quint16 command = normalizeCommand(frame.command);
    quint16 triggerCount = frame.count;

    if (command == kLegacyCommandTrigger) {
        queueManager_->enqueuePair(frame.count, frame.pointer);
        currentWaitingSlot_ = WaitingSlot{frame.count, frame.pointer};
        currentCycleCount_ = frame.count;
        pointerByCount_.insert(frame.count, frame.pointer);
    } else if (command == kLegacyCommandArm2) {
        const quint16 count = currentCycleCount_.value_or(frame.count);
        currentWaitingSlot_ = WaitingSlot{count, frame.pointer};
        pointerByCount_.insert(count, frame.pointer);
        triggerCount = count;
    } else if (command == kLegacyCommandEnd) {
        triggerCount = currentCycleCount_.value_or(frame.count);
        currentWaitingSlot_.reset();
    }

    if (command == kLegacyCommandTrigger || command == kLegacyCommandArm2 || command == kLegacyCommandEnd) {
        sendTrigger(command, triggerCount);
    }
}

void LegacyCameraWorkflow::handleCameraPayload(QByteArray payload)
{
    const auto parsed = parseLegacyPayload(payload);
    if (!parsed.has_value()) {
        emit warning(QStringLiteral("legacy camera payload is invalid"));
        return;
    }

    const auto cameraCount = extractCameraCount(payload, parsed->embeddedCount);
    const auto slot = resolveSlot(cameraCount);
    if (!slot.has_value()) {
        emit warning(QStringLiteral("legacy camera payload has no matching queue slot"));
        return;
    }

    const quint16 count = cameraCount.value_or(slot->count);
    const QByteArray storedPayload = maybeWrapCameraData(payload, slot->count, cameraCount);
    const bool stored = queueManager_->storeCameraData(parsed->armId, count, slot->pointer, storedPayload);
    if (!stored) {
        emit warning(QStringLiteral("legacy camera payload did not match queue item"));
        return;
    }

    sendStageFeedback(parsed->armId);
    if (correlationMode_ == config::CameraCorrelationMode::Sequential) {
        currentWaitingSlot_.reset();
    }
}

quint16 LegacyCameraWorkflow::normalizeCommand(quint16 command) const
{
    if (command == 1) {
        return kLegacyCommandTrigger;
    }
    return command;
}

void LegacyCameraWorkflow::sendTrigger(quint16 command, quint16 count)
{
    const QByteArray payload = protocol::writeU16BE(command) + protocol::writeU16BE(count);
    if (!triggerSender_) {
        emit warning(QStringLiteral("legacy camera trigger sender is not configured"));
        emit cameraTriggerSent(payload, false);
        return;
    }

    const bool ok = triggerSender_(payload);
    emit cameraTriggerSent(payload, ok);
}

bool LegacyCameraWorkflow::sendStageFeedback(int armId)
{
    const QByteArray payload = QByteArray::number(armId) + QByteArray("Done");
    if (!feedbackSender_) {
        emit warning(QStringLiteral("legacy stage feedback sender is not configured"));
        emit stageFeedbackSent(payload, false);
        return false;
    }

    const bool ok = feedbackSender_(payload);
    emit stageFeedbackSent(payload, ok);
    return ok;
}

std::optional<LegacyCameraWorkflow::WaitingSlot> LegacyCameraWorkflow::resolveSlot(
    std::optional<quint16> cameraCount) const
{
    if (correlationMode_ == config::CameraCorrelationMode::Counted && cameraCount.has_value()) {
        const auto it = pointerByCount_.find(*cameraCount);
        if (it == pointerByCount_.end()) {
            return std::nullopt;
        }
        return WaitingSlot{*cameraCount, *it};
    }
    return currentWaitingSlot_;
}

std::optional<LegacyCameraWorkflow::LegacyPayload> LegacyCameraWorkflow::parseLegacyPayload(
    const QByteArray& payload) const
{
    QString text = QString::fromUtf8(payload).trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    if (text.endsWith(QLatin1Char('E'))) {
        text.chop(1);
    }
    if (text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')'))) {
        text = text.mid(1, text.size() - 2).trimmed();
    }

    const QStringList tokens = text.split(',');
    if (tokens.isEmpty()) {
        return std::nullopt;
    }

    const QString flagText = tokens.first().trimmed();
    const auto flag = parseU16Text(flagText);
    if (!flag.has_value()) {
        return std::nullopt;
    }

    int armId = static_cast<int>(*flag) / 1000;
    if (armId != 1 && armId != 2 && !flagText.isEmpty() && flagText.front().isDigit()) {
        armId = flagText.left(1).toInt();
    }
    if (armId != 1 && armId != 2) {
        return std::nullopt;
    }

    std::optional<quint16> embeddedCount;
    if (tokens.size() >= 2) {
        embeddedCount = parseU16Text(tokens.at(1));
    }

    return LegacyPayload{armId, embeddedCount};
}

std::optional<quint16> LegacyCameraWorkflow::extractCameraCount(
    const QByteArray& payload,
    std::optional<quint16> segmentCount) const
{
    if (countExtractMode_ == config::CameraCountExtractMode::Disabled) {
        return std::nullopt;
    }
    if (countExtractMode_ == config::CameraCountExtractMode::Ascii) {
        return extractAsciiCount(payload);
    }
    if (countExtractMode_ == config::CameraCountExtractMode::Binary) {
        return extractBinaryCount(payload);
    }

    if (segmentCount.has_value()) {
        return segmentCount;
    }
    if (const auto asciiCount = extractAsciiCount(payload); asciiCount.has_value()) {
        return asciiCount;
    }
    if (isMostlyPrintableText(payload)) {
        return std::nullopt;
    }
    return extractBinaryCount(payload);
}

QByteArray LegacyCameraWorkflow::maybeWrapCameraData(
    const QByteArray& payload,
    quint16 slotCount,
    std::optional<quint16> cameraCount) const
{
    if (payload.isEmpty() || cameraCount.has_value()) {
        return payload;
    }

    QString text = QString::fromUtf8(payload).trimmed();
    if (text.isEmpty()) {
        return payload;
    }

    bool hadEnd = false;
    if (text.endsWith(QLatin1Char('E'))) {
        text.chop(1);
        hadEnd = true;
    }

    const qsizetype commaIndex = text.indexOf(QLatin1Char(','));
    if (commaIndex < 0) {
        return payload;
    }

    const QStringList parts = text.split(',');
    if (parts.size() >= 2 && parseU16Text(parts.at(1)).has_value()) {
        return payload;
    }

    text.insert(commaIndex + 1, QString::number(slotCount) + QStringLiteral(","));
    if (hadEnd) {
        text.append(QLatin1Char('E'));
    }
    return text.toUtf8();
}

} // namespace spray::core
