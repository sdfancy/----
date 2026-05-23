#include "protocol/ByteCodec.h"

namespace spray::protocol {

quint16 readU16BE(const QByteArray& bytes, qsizetype offset)
{
    const auto high = static_cast<unsigned char>(bytes.at(offset));
    const auto low = static_cast<unsigned char>(bytes.at(offset + 1));
    return static_cast<quint16>((high << 8) | low);
}

QByteArray writeU16BE(quint16 value)
{
    QByteArray out;
    out.resize(2);
    out[0] = static_cast<char>((value >> 8) & 0xFF);
    out[1] = static_cast<char>(value & 0xFF);
    return out;
}

QString toHex(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

} // namespace spray::protocol
