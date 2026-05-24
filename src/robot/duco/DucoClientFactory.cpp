#include "robot/duco/DucoClientFactory.h"

namespace spray::robot::duco {

namespace {

class UnavailableDucoClient final : public IDucoClient {
public:
    int open() override
    {
        return -1;
    }

    int close() override
    {
        return 0;
    }

    void rpcHeartbeat(int timeMs) override
    {
        Q_UNUSED(timeMs)
    }

    int powerOn(bool block) override
    {
        Q_UNUSED(block)
        return -1;
    }

    int enable(bool block) override
    {
        Q_UNUSED(block)
        return -1;
    }

    int stop(bool block) override
    {
        Q_UNUSED(block)
        return -1;
    }

    int pause(bool block) override
    {
        Q_UNUSED(block)
        return -1;
    }

    int resume(bool block) override
    {
        Q_UNUSED(block)
        return -1;
    }

    DucoRobotState getRobotState() override
    {
        DucoRobotState state;
        state.ok = false;
        state.message = QStringLiteral("DUCO SDK is not enabled");
        return state;
    }

    int moveJPose2(const Pose6d& pose,
                   double velocity,
                   double acceleration,
                   double radius,
                   const Joint6d& qNear,
                   const QString& tool,
                   const QString& wobj,
                   bool block) override
    {
        Q_UNUSED(pose)
        Q_UNUSED(velocity)
        Q_UNUSED(acceleration)
        Q_UNUSED(radius)
        Q_UNUSED(qNear)
        Q_UNUSED(tool)
        Q_UNUSED(wobj)
        Q_UNUSED(block)
        return -1;
    }

    int moveL(const Pose6d& pose,
              double velocity,
              double acceleration,
              double radius,
              const Joint6d& qNear,
              const QString& tool,
              const QString& wobj,
              bool block) override
    {
        Q_UNUSED(pose)
        Q_UNUSED(velocity)
        Q_UNUSED(acceleration)
        Q_UNUSED(radius)
        Q_UNUSED(qNear)
        Q_UNUSED(tool)
        Q_UNUSED(wobj)
        Q_UNUSED(block)
        return -1;
    }

    int setToolDigitalOut(int channel, bool value, bool block) override
    {
        Q_UNUSED(channel)
        Q_UNUSED(value)
        Q_UNUSED(block)
        return -1;
    }

    int setStandardDigitalOut(int channel, bool value, bool block) override
    {
        Q_UNUSED(channel)
        Q_UNUSED(value)
        Q_UNUSED(block)
        return -1;
    }
};

class UnavailableDucoClientFactory final : public IDucoClientFactory {
public:
    std::unique_ptr<IDucoClient> createClient(DucoClientRole role) override
    {
        Q_UNUSED(role)
        return std::make_unique<UnavailableDucoClient>();
    }
};

} // namespace

std::unique_ptr<IDucoClientFactory> createDucoClientFactory(const config::RobotConfig& config)
{
    Q_UNUSED(config)

    return std::make_unique<UnavailableDucoClientFactory>();
}

} // namespace spray::robot::duco
