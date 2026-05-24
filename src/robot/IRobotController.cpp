#include "robot/IRobotController.h"

namespace spray::robot {

IRobotController::IRobotController(QObject* parent)
    : QObject(parent)
{
}

IRobotController::~IRobotController() = default;

} // namespace spray::robot
