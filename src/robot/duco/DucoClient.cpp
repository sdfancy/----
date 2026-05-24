#include "robot/duco/DucoClient.h"

namespace spray::robot::duco {

IDucoClient::~IDucoClient() = default;
IDucoClientFactory::~IDucoClientFactory() = default;

QString roleName(DucoClientRole role)
{
    switch (role) {
    case DucoClientRole::Motion:
        return QStringLiteral("motion");
    case DucoClientRole::Control:
        return QStringLiteral("control");
    case DucoClientRole::Heartbeat:
        return QStringLiteral("heartbeat");
    case DucoClientRole::Status:
        return QStringLiteral("status");
    }
    return QStringLiteral("unknown");
}

} // namespace spray::robot::duco
