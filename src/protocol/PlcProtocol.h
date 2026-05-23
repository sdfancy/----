#pragma once

#include "domain/Types.h"
#include "protocol/ProtocolResult.h"

#include <QByteArray>
#include <QString>

namespace spray::protocol {

struct PlcEnqueueFrame {
    quint16 command = 0;
    quint16 count = 0;
    quint16 pointer = 0;
    QByteArray extra;
};

struct PlcDequeueFrame {
    quint16 arm1Pointer = 0;
    quint16 arm2Pointer = 0;
};

ProtocolResult<PlcEnqueueFrame> parseEnqueueFrame(const QByteArray& raw);
ProtocolResult<PlcDequeueFrame> parseDequeueFrame(const QByteArray& raw);
QByteArray buildFeedback(int armId, domain::FeedbackStage stage);
QString describeFrame(const QString& channel, const QByteArray& raw);

} // namespace spray::protocol
