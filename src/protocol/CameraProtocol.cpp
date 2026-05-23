#include "protocol/CameraProtocol.h"

#include <QRegularExpression>
#include <QStringList>

#include <optional>

namespace spray::protocol {
namespace {

QByteArray trimLeftFrameWhitespace(QByteArray buffer)
{
    while (!buffer.isEmpty()) {
        const char c = buffer.at(0);
        if (c != '\r' && c != '\n' && c != '\t' && c != ' ') {
            break;
        }
        buffer.remove(0, 1);
    }
    return buffer;
}

bool isReadyText(const QString& text)
{
    const QString upper = text.trimmed().toUpper();
    return upper == QStringLiteral("READY")
        || upper == QStringLiteral("(READY)E")
        || upper == QStringLiteral("READYE")
        || upper == QStringLiteral("(READY)");
}

bool looksLike2dLine(const QByteArray& payload)
{
    const QString text = QString::fromUtf8(payload).trimmed();
    if (text.isEmpty() || text.size() > 128) {
        return false;
    }
    if (isReadyText(text)) {
        return true;
    }

    const QStringList parts = text.split(',');
    if (parts.isEmpty()) {
        return false;
    }

    static const QRegularExpression digitsOnly(QStringLiteral("^\\d+$"));
    return digitsOnly.match(parts.first().trimmed()).hasMatch();
}

int firstNewlineIndex(const QByteArray& buffer, qsizetype* newlineLength)
{
    int bestIndex = -1;
    qsizetype bestLength = 0;
    const QList<QByteArray> separators = {
        QByteArray("\r\n"),
        QByteArray("\n"),
        QByteArray("\r"),
    };

    for (const auto& separator : separators) {
        const int index = buffer.indexOf(separator);
        if (index >= 0 && (bestIndex < 0 || index < bestIndex)) {
            bestIndex = index;
            bestLength = separator.size();
        }
    }

    if (newlineLength) {
        *newlineLength = bestLength;
    }
    return bestIndex;
}

std::optional<quint16> parseU16Text(const QString& text)
{
    bool ok = false;
    const uint value = text.toUInt(&ok);
    if (!ok || value > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<quint16>(value);
}

} // namespace

ProtocolResult<Parsed2dFrame> parse2dFrame(const QByteArray& raw)
{
    QString text = QString::fromUtf8(raw).trimmed();
    if (text.isEmpty()) {
        return ProtocolResult<Parsed2dFrame>::fail(QStringLiteral("2D frame is empty"));
    }
    if (isReadyText(text)) {
        return ProtocolResult<Parsed2dFrame>::ok({Parsed2dFrameKind::Ready, 0, {}});
    }

    if (text.endsWith(QLatin1Char('E'))) {
        text.chop(1);
        text = text.trimmed();
    }
    if (text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')'))) {
        text = text.mid(1, text.size() - 2).trimmed();
    }
    if (isReadyText(text)) {
        return ProtocolResult<Parsed2dFrame>::ok({Parsed2dFrameKind::Ready, 0, {}});
    }

    QStringList parts = text.split(',');
    if (parts.isEmpty()) {
        return ProtocolResult<Parsed2dFrame>::fail(QStringLiteral("2D frame has no fields"));
    }

    QString countText = parts.takeFirst().trimmed();
    auto count = parseU16Text(countText);
    if (!count.has_value()) {
        static const QRegularExpression digits(QStringLiteral("\\d+"));
        const QRegularExpressionMatch match = digits.match(countText);
        if (!match.hasMatch()) {
            return ProtocolResult<Parsed2dFrame>::fail(
                QStringLiteral("2D frame count is invalid: %1").arg(countText));
        }
        count = parseU16Text(match.captured(0));
        if (!count.has_value()) {
            return ProtocolResult<Parsed2dFrame>::fail(
                QStringLiteral("2D frame count is out of range: %1").arg(match.captured(0)));
        }
    }

    QStringList typeFields;
    for (const auto& part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            typeFields.append(trimmed);
        }
    }

    return ProtocolResult<Parsed2dFrame>::ok({
        Parsed2dFrameKind::Result,
        *count,
        typeFields.isEmpty() ? QStringLiteral("00,00") : typeFields.join(QStringLiteral(",")),
    });
}

FrameDrainResult drain2dFrames(const QByteArray& buffer)
{
    FrameDrainResult result;
    QByteArray remaining = trimLeftFrameWhitespace(buffer);

    while (!remaining.isEmpty()) {
        const int endIndex = remaining.indexOf(")E");
        qsizetype newlineLength = 0;
        const int newlineIndex = firstNewlineIndex(remaining, &newlineLength);
        if (newlineIndex >= 0 && (endIndex < 0 || newlineIndex < endIndex)) {
            const QByteArray frame = remaining.left(newlineIndex).trimmed();
            remaining = trimLeftFrameWhitespace(remaining.mid(newlineIndex + newlineLength));
            if (!frame.isEmpty()) {
                result.frames.append(frame);
            }
            continue;
        }

        if (endIndex >= 0) {
            result.frames.append(remaining.left(endIndex + 2));
            remaining = trimLeftFrameWhitespace(remaining.mid(endIndex + 2));
            continue;
        }

        const QByteArray candidate = remaining.trimmed();
        if (looksLike2dLine(candidate)) {
            result.frames.append(candidate);
            remaining.clear();
        }
        break;
    }

    result.remaining = remaining;
    return result;
}

FrameDrainResult drain3dFrames(const QByteArray& buffer)
{
    FrameDrainResult result;
    QString remainingText = QString::fromLatin1(buffer);
    static const QRegularExpression framePattern(QStringLiteral("\\([^()]*\\)EA\\([^()]*\\)E"));

    while (!remainingText.isEmpty()) {
        const QRegularExpressionMatch match = framePattern.match(remainingText);
        if (!match.hasMatch()) {
            break;
        }

        const QString frame = match.captured(0);
        result.frames.append(frame.toLatin1());
        remainingText = remainingText.mid(match.capturedEnd(0));
    }

    result.remaining = remainingText.toLatin1();
    return result;
}

QList<QByteArray> split3dSegments(const QByteArray& frame)
{
    QList<QByteArray> segments;
    const QList<QByteArray> parts = frame.split('A');
    for (const auto& part : parts) {
        const QByteArray trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            segments.append(trimmed);
        }
    }
    return segments;
}

ProtocolResult<Parsed3dSegment> parse3dSegment(const QByteArray& segment)
{
    QString text = QString::fromUtf8(segment).trimmed();
    if (text.isEmpty()) {
        return ProtocolResult<Parsed3dSegment>::fail(QStringLiteral("3D segment is empty"));
    }
    if (!text.endsWith(QLatin1Char('E'))) {
        text.append(QLatin1Char('E'));
    }

    QString body = text.left(text.size() - 1).trimmed();
    if (body.startsWith(QLatin1Char('(')) && body.endsWith(QLatin1Char(')'))) {
        body = body.mid(1, body.size() - 2).trimmed();
    }

    const QStringList rawTokens = body.split(',');
    QStringList tokens;
    for (const auto& token : rawTokens) {
        const QString trimmed = token.trimmed();
        if (!trimmed.isEmpty()) {
            tokens.append(trimmed);
        }
    }
    if (tokens.size() < 2) {
        return ProtocolResult<Parsed3dSegment>::fail(QStringLiteral("3D segment has too few fields"));
    }

    const auto flag = parseU16Text(tokens.at(0));
    const auto count = parseU16Text(tokens.at(1));
    if (!flag.has_value() || !count.has_value()) {
        return ProtocolResult<Parsed3dSegment>::fail(QStringLiteral("3D segment flag or count is invalid"));
    }

    const int armId = static_cast<int>(*flag) / 1000;
    if (armId != 1 && armId != 2) {
        return ProtocolResult<Parsed3dSegment>::fail(
            QStringLiteral("3D segment arm id is invalid: %1").arg(armId));
    }

    return ProtocolResult<Parsed3dSegment>::ok({armId, *count, text.toLatin1()});
}

} // namespace spray::protocol
