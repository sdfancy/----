#pragma once

#include <QByteArray>

namespace spray::domain {

QByteArray formatDefaultPayload(int armId, quint16 count);
bool isDefaultPayload(const QByteArray& payload);

} // namespace spray::domain
