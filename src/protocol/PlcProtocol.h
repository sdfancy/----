#pragma once

#include "domain/Types.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace spray::protocol {

struct ProtocolError {
    QString message;
};

template <typename T>
struct ProtocolResult {
    std::optional<T> value;
    std::optional<ProtocolError> error;

    static ProtocolResult ok(T parsed)
    {
        return {std::move(parsed), std::nullopt};
    }

    static ProtocolResult fail(QString message)
    {
        return {std::nullopt, ProtocolError{std::move(message)}};
    }

    bool hasValue() const
    {
        return value.has_value();
    }
};

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
