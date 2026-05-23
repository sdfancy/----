#include "domain/Payloads.h"

namespace spray::domain {

QByteArray formatDefaultPayload(int armId, quint16 count)
{
    const int marker = armId == 2 ? 2000 : 1000;
    return QByteArray("(") + QByteArray::number(marker) + QByteArray(",")
        + QByteArray::number(count) + QByteArray(")E");
}

bool isDefaultPayload(const QByteArray& payload)
{
    return payload.startsWith("(1000,") || payload.startsWith("(2000,");
}

} // namespace spray::domain
