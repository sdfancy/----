#pragma once

#include <QByteArray>
#include <QString>

namespace spray::protocol {

quint16 readU16BE(const QByteArray& bytes, qsizetype offset);
QByteArray writeU16BE(quint16 value);
QString toHex(const QByteArray& bytes);

} // namespace spray::protocol
