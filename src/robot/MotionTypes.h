#pragma once

#include "core/QueueManager.h"

#include <QList>
#include <QString>

namespace spray::robot {

struct Pose6d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
};

struct Joint6d {
    double j1 = 0.0;
    double j2 = 0.0;
    double j3 = 0.0;
    double j4 = 0.0;
    double j5 = 0.0;
    double j6 = 0.0;
};

enum class MotionSegmentType {
    MoveJPose2,
    MoveL,
    ToolDigitalOut,
    StandardDigitalOut,
};

struct MotionSegment {
    MotionSegmentType type = MotionSegmentType::MoveL;
    Pose6d pose;
    Joint6d qNear;
    double velocity = 0.0;
    double acceleration = 0.0;
    double radius = 0.0;
    QString tool = QStringLiteral("default");
    QString wobj = QStringLiteral("default");
    int ioChannel = 0;
    bool ioValue = false;
};

struct MotionRecipe {
    int armId = 0;
    bool enabled = false;
    QString tool = QStringLiteral("default");
    QString wobj = QStringLiteral("default");
    Joint6d qNear;
    QList<int> poseValueIndices{0, 1, 2, 3, 4, 5};
    double approachSpeed = 1.0;
    double lineSpeed = 1.0;
    double acceleration = 1.0;
    double radius = 0.0;
    MotionSegmentType sprayIoType = MotionSegmentType::ToolDigitalOut;
    int sprayIoChannel = 1;
};

struct ArmPayload {
    int flag = 0;
    int armId = 0;
    quint16 count = 0;
    QList<double> values;
    bool defaultNoop = false;
};

struct PlannedRobotTask {
    core::RobotTask task;
    QList<MotionSegment> segments;
};

struct MotionPlanResult {
    bool ok = false;
    ArmPayload payload;
    PlannedRobotTask planned;
    QString message;
};

} // namespace spray::robot
