#include "protocol/PlcProtocol.h"

#include "protocol/ByteCodec.h"

namespace spray::protocol {

ProtocolResult<PlcEnqueueFrame> parseEnqueueFrame(const QByteArray& raw)
{
    if (raw.size() < 6) {
        return ProtocolResult<PlcEnqueueFrame>::fail(
            QStringLiteral("PLC enqueue data too short: %1").arg(raw.size()));
    }

    return ProtocolResult<PlcEnqueueFrame>::ok({
        readU16BE(raw, 0),
        readU16BE(raw, 2),
        readU16BE(raw, 4),
        raw.mid(6),
    });
}

ProtocolResult<PlcDequeueFrame> parseDequeueFrame(const QByteArray& raw)
{
    if (raw.size() != 4) {
        return ProtocolResult<PlcDequeueFrame>::fail(
            QStringLiteral("PLC dequeue data must be 4 bytes, got %1").arg(raw.size()));
    }

    return ProtocolResult<PlcDequeueFrame>::ok({
        readU16BE(raw, 0),
        readU16BE(raw, 2),
    });
}

QByteArray buildFeedback(int armId, domain::FeedbackStage stage)
{
    const char suffix = stage == domain::FeedbackStage::Normal ? 'N' : 'D';
    return QByteArray::number(armId) + QByteArray(1, suffix);
}

QString describeFrame(const QString& channel, const QByteArray& raw)
{
    return QStringLiteral("[%1] %2").arg(channel, toHex(raw));
}

} // namespace spray::protocol
