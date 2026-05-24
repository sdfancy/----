#pragma once

#include "config/AppConfig.h"
#include "robot/duco/DucoClient.h"

#include <memory>

namespace spray::robot::duco {

std::unique_ptr<IDucoClientFactory> createDucoClientFactory(const config::RobotConfig& config);

} // namespace spray::robot::duco
