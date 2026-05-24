#pragma once

#include "robot/IRobotController.h"

#include <QQueue>
#include <QSet>

namespace spray::robot {

class FakeRobotController final : public IRobotController {
    Q_OBJECT

public:
    explicit FakeRobotController(int acceptDelayMs = 20, int finishDelayMs = 80, QObject* parent = nullptr);

    RobotCommandResult connectRobot() override;
    void disconnectRobot() override;
    RobotCommandResult prepare() override;
    void enqueueTask(const core::RobotTask& task) override;
    RobotCommandResult stop() override;
    RobotCommandResult pause() override;
    RobotCommandResult resume() override;
    RobotStatus readStatus() override;
    int pendingCount(int armId) const;

private:
    void tryStartNext(int armId);

    struct ArmState {
        bool busy = false;
        QQueue<core::RobotTask> pending;
    };

    QHash<int, ArmState> arms_;
    int acceptDelayMs_ = 20;
    int finishDelayMs_ = 80;
};

} // namespace spray::robot
