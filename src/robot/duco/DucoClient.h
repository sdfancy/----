#pragma once

#include "robot/MotionTypes.h"

#include <QString>

#include <memory>

namespace spray::robot::duco {

enum class DucoClientRole {
    Motion,
    Control,
    Heartbeat,
    Status,
};

struct DucoRobotState {
    bool ok = true;
    int robotState = 0;
    int programState = 0;
    int safetyState = 0;
    int operationMode = 0;
    bool moving = false;
    QString message;
};

QString roleName(DucoClientRole role);

class IDucoClient {
public:
    virtual ~IDucoClient();

    virtual int open() = 0;
    virtual int close() = 0;
    virtual void rpcHeartbeat(int timeMs) = 0;
    virtual int powerOn(bool block) = 0;
    virtual int enable(bool block) = 0;
    virtual int stop(bool block) = 0;
    virtual int pause(bool block) = 0;
    virtual int resume(bool block) = 0;
    virtual DucoRobotState getRobotState() = 0;
    virtual int moveJPose2(const Pose6d& pose,
                           double velocity,
                           double acceleration,
                           double radius,
                           const Joint6d& qNear,
                           const QString& tool,
                           const QString& wobj,
                           bool block) = 0;
    virtual int moveL(const Pose6d& pose,
                      double velocity,
                      double acceleration,
                      double radius,
                      const Joint6d& qNear,
                      const QString& tool,
                      const QString& wobj,
                      bool block) = 0;
    virtual int setToolDigitalOut(int channel, bool value, bool block) = 0;
    virtual int setStandardDigitalOut(int channel, bool value, bool block) = 0;
};

class IDucoClientFactory {
public:
    virtual ~IDucoClientFactory();

    virtual std::unique_ptr<IDucoClient> createClient(DucoClientRole role) = 0;
};

} // namespace spray::robot::duco
