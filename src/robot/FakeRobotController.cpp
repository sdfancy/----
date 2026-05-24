#include "robot/FakeRobotController.h"

#include <QTimer>

namespace spray::robot {

FakeRobotController::FakeRobotController(int acceptDelayMs, int finishDelayMs, QObject* parent)
    : IRobotController(parent)
    , acceptDelayMs_(std::max(0, acceptDelayMs))
    , finishDelayMs_(std::max(0, finishDelayMs))
{
}

RobotCommandResult FakeRobotController::connectRobot()
{
    return {true, QStringLiteral("fake robot connected")};
}

void FakeRobotController::disconnectRobot()
{
}

RobotCommandResult FakeRobotController::prepare()
{
    emit statusChanged(readStatus());
    return {true, QStringLiteral("fake robot prepared")};
}

void FakeRobotController::enqueueTask(const core::RobotTask& task)
{
    arms_[task.armId].pending.enqueue(task);
    tryStartNext(task.armId);
}

RobotCommandResult FakeRobotController::stop()
{
    for (auto it = arms_.begin(); it != arms_.end(); ++it) {
        it->pending.clear();
    }
    emit statusChanged(readStatus());
    return {true, QStringLiteral("fake robot stopped")};
}

RobotCommandResult FakeRobotController::pause()
{
    return {true, QStringLiteral("fake robot paused")};
}

RobotCommandResult FakeRobotController::resume()
{
    return {true, QStringLiteral("fake robot resumed")};
}

RobotStatus FakeRobotController::readStatus()
{
    RobotStatus status;
    status.connectionState = RobotConnectionState::Prepared;
    status.message = QStringLiteral("fake robot ready");
    for (auto it = arms_.cbegin(); it != arms_.cend(); ++it) {
        if (it->busy || !it->pending.isEmpty()) {
            status.moving = true;
            break;
        }
    }
    return status;
}

int FakeRobotController::pendingCount(int armId) const
{
    const auto it = arms_.find(armId);
    if (it == arms_.end()) {
        return 0;
    }
    return it->pending.size() + (it->busy ? 1 : 0);
}

void FakeRobotController::tryStartNext(int armId)
{
    auto& state = arms_[armId];
    if (state.busy || state.pending.isEmpty()) {
        return;
    }

    state.busy = true;
    const auto task = state.pending.dequeue();
    QTimer::singleShot(acceptDelayMs_, this, [this, task]() {
        emit taskAccepted(task);
        QTimer::singleShot(finishDelayMs_, this, [this, task]() {
            emit taskFinished(task, true, QStringLiteral("fake robot finished"));
            arms_[task.armId].busy = false;
            tryStartNext(task.armId);
        });
    });
}

} // namespace spray::robot
