#pragma once

#include "robot/MotionTypes.h"

#include <QByteArray>

namespace spray::robot {

class MotionPlanner {
public:
    static MotionPlanResult parseArmPayload(const QByteArray& payload);
    static MotionPlanResult plan(const core::RobotTask& task, const MotionRecipe& recipe);
};

} // namespace spray::robot
