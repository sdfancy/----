#pragma once

#include "protocol/ProtocolResult.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace spray::protocol {

enum class Parsed2dFrameKind {
    Ready,
    Result,
};

struct Parsed2dFrame {
    Parsed2dFrameKind kind = Parsed2dFrameKind::Result;
    quint16 count = 0;
    QString partType;
};

struct Parsed3dSegment {
    int armId = 0;
    quint16 count = 0;
    QByteArray payload;
};

struct FrameDrainResult {
    QList<QByteArray> frames;
    QByteArray remaining;
};

ProtocolResult<Parsed2dFrame> parse2dFrame(const QByteArray& raw);
FrameDrainResult drain2dFrames(const QByteArray& buffer);
FrameDrainResult drain3dFrames(const QByteArray& buffer);
QList<QByteArray> split3dSegments(const QByteArray& frame);
ProtocolResult<Parsed3dSegment> parse3dSegment(const QByteArray& segment);

} // namespace spray::protocol
