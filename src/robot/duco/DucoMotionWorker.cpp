#include "robot/duco/DucoMotionWorker.h"

namespace spray::robot::duco {

DucoMotionWorker::DucoMotionWorker(IDucoClient* motionClient)
    : motionClient_(motionClient)
{
}

bool DucoMotionWorker::execute(const PlannedRobotTask& planned, QString* message, QStringList* warnings)
{
    if (!motionClient_) {
        if (message) {
            *message = QStringLiteral("DUCO motion client is not connected");
        }
        return false;
    }

    bool sprayIoActive = false;
    MotionSegment activeIo;

    for (const auto& segment : planned.segments) {
        const int result = executeSegment(segment);
        if (result == -1) {
            if (sprayIoActive) {
                closeSprayIo(activeIo, warnings);
            }
            if (message) {
                *message = QStringLiteral("DUCO motion segment failed");
            }
            return false;
        }

        if ((segment.type == MotionSegmentType::ToolDigitalOut
             || segment.type == MotionSegmentType::StandardDigitalOut)
            && segment.ioValue) {
            activeIo = segment;
            activeIo.ioValue = false;
            sprayIoActive = true;
        } else if ((segment.type == MotionSegmentType::ToolDigitalOut
                    || segment.type == MotionSegmentType::StandardDigitalOut)
                   && !segment.ioValue) {
            sprayIoActive = false;
        }
    }

    if (message) {
        *message = QStringLiteral("DUCO motion task finished");
    }
    return true;
}

int DucoMotionWorker::executeSegment(const MotionSegment& segment)
{
    switch (segment.type) {
    case MotionSegmentType::MoveJPose2:
        return motionClient_->moveJPose2(segment.pose,
                                         segment.velocity,
                                         segment.acceleration,
                                         segment.radius,
                                         segment.qNear,
                                         segment.tool,
                                         segment.wobj,
                                         true);
    case MotionSegmentType::MoveL:
        return motionClient_->moveL(segment.pose,
                                    segment.velocity,
                                    segment.acceleration,
                                    segment.radius,
                                    segment.qNear,
                                    segment.tool,
                                    segment.wobj,
                                    true);
    case MotionSegmentType::ToolDigitalOut:
        return motionClient_->setToolDigitalOut(segment.ioChannel, segment.ioValue, true);
    case MotionSegmentType::StandardDigitalOut:
        return motionClient_->setStandardDigitalOut(segment.ioChannel, segment.ioValue, true);
    }
    return -1;
}

void DucoMotionWorker::closeSprayIo(const MotionSegment& activeIo, QStringList* warnings)
{
    const int result = executeSegment(activeIo);
    if (result == -1 && warnings) {
        warnings->append(QStringLiteral("DUCO spray IO close failed"));
    }
}

} // namespace spray::robot::duco
