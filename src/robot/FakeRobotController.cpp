#include "robot/FakeRobotController.h"

#include <QTimer>

namespace spray::robot {

FakeRobotController::FakeRobotController(int acceptDelayMs, int finishDelayMs, QObject* parent)
    : QObject(parent)
    , acceptDelayMs_(std::max(0, acceptDelayMs))
    , finishDelayMs_(std::max(0, finishDelayMs))
{
}

void FakeRobotController::enqueueTask(const core::RobotTask& task)
{
    arms_[task.armId].pending.enqueue(task);
    tryStartNext(task.armId);
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
