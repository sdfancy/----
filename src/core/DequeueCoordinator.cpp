#include "core/DequeueCoordinator.h"

#include "protocol/PlcProtocol.h"

namespace spray::core {

DequeueCoordinator::DequeueCoordinator(QueueManager* queueManager,
                                       io::PlcEndpoint* plcEndpoint,
                                       robot::IRobotController* robot,
                                       QObject* parent)
    : QObject(parent)
    , queueManager_(queueManager)
    , plcEndpoint_(plcEndpoint)
    , robot_(robot)
{
    connect(robot_, &robot::IRobotController::taskAccepted, this, &DequeueCoordinator::onTaskAccepted);
    connect(robot_, &robot::IRobotController::taskFinished, this, &DequeueCoordinator::onTaskFinished);
}

void DequeueCoordinator::onDequeueFrame(protocol::PlcDequeueFrame frame)
{
    const auto tasks = queueManager_->handleDequeuePointers(frame);
    for (const auto& task : tasks) {
        robot_->enqueueTask(task);
        emit taskDispatched(task);
    }
}

void DequeueCoordinator::onTaskAccepted(core::RobotTask task)
{
    queueManager_->markTaskAccepted(task);
    const auto feedback = protocol::buildFeedback(task.armId, domain::FeedbackStage::Normal);
    const bool ok = plcEndpoint_->sendDequeueFeedback(feedback);
    emit feedbackSent(feedback, ok);
}

void DequeueCoordinator::onTaskFinished(core::RobotTask task, bool ok, QString message)
{
    Q_UNUSED(message)

    if (!ok) {
        return;
    }

    queueManager_->markTaskDone(task);
    const auto feedback = protocol::buildFeedback(task.armId, domain::FeedbackStage::Done);
    const bool sent = plcEndpoint_->sendDequeueFeedback(feedback);
    emit feedbackSent(feedback, sent);
}

} // namespace spray::core
