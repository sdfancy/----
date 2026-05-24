#pragma once

#include "robot/MotionTypes.h"
#include "robot/duco/DucoClient.h"

#include <QStringList>

namespace spray::robot::duco {

class DucoMotionWorker {
public:
    explicit DucoMotionWorker(IDucoClient* motionClient);

    bool execute(const PlannedRobotTask& planned, QString* message, QStringList* warnings);

private:
    int executeSegment(const MotionSegment& segment);
    void closeSprayIo(const MotionSegment& activeIo, QStringList* warnings);

    IDucoClient* motionClient_ = nullptr;
};

} // namespace spray::robot::duco
