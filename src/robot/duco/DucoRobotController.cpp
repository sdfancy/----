#include "robot/duco/DucoRobotController.h"

#include "robot/MotionPlanner.h"
#include "robot/duco/DucoMotionWorker.h"

#include <array>

#include <QTimer>

namespace spray::robot::duco {

DucoRobotController::DucoRobotController(config::RobotConfig config,
                                         std::unique_ptr<IDucoClientFactory> clientFactory,
                                         QObject* parent)
    : IRobotController(parent)
    , config_(std::move(config))
    , clientFactory_(std::move(clientFactory))
{
    status_.connectionState = RobotConnectionState::Disconnected;
}

RobotCommandResult DucoRobotController::connectRobot()
{
    status_.connectionState = RobotConnectionState::Connecting;
    emit statusChanged(status_);

    QString errorMessage;
    if (!createRoleClients(&errorMessage)) {
        status_.connectionState = RobotConnectionState::Faulted;
        status_.message = errorMessage;
        emit warning(errorMessage);
        emit statusChanged(status_);
        return {false, errorMessage};
    }

    const std::array<IDucoClient*, 4> clients{
        motionClient_.get(),
        controlClient_.get(),
        heartbeatClient_.get(),
        statusClient_.get(),
    };
    for (auto* client : clients) {
        if (!client || client->open() == -1) {
            const auto message = QStringLiteral("DUCO open failed");
            status_.connectionState = RobotConnectionState::Faulted;
            status_.message = message;
            emit warning(message);
            emit statusChanged(status_);
            return {false, message};
        }
    }

    status_.connectionState = RobotConnectionState::Connected;
    status_.message = QStringLiteral("DUCO connected");
    emit statusChanged(status_);
    return {true, status_.message};
}

void DucoRobotController::disconnectRobot()
{
    const std::array<IDucoClient*, 4> clients{
        motionClient_.get(),
        controlClient_.get(),
        heartbeatClient_.get(),
        statusClient_.get(),
    };
    for (auto* client : clients) {
        if (client) {
            client->close();
        }
    }

    status_ = {};
    status_.connectionState = RobotConnectionState::Disconnected;
    status_.message = QStringLiteral("DUCO disconnected");
    emit statusChanged(status_);
}

RobotCommandResult DucoRobotController::prepare()
{
    auto connected = ensureConnected();
    if (!connected.ok) {
        return connected;
    }

    heartbeatClient_->rpcHeartbeat(config_.heartbeatMs);

    if (config_.autoPowerOn && controlClient_->powerOn(true) == -1) {
        return failWithFault(QStringLiteral("DUCO power_on failed"));
    }
    if (config_.autoEnable && controlClient_->enable(true) == -1) {
        return failWithFault(QStringLiteral("DUCO enable failed"));
    }

    auto state = statusClient_->getRobotState();
    if (!state.ok) {
        return failWithFault(state.message.isEmpty()
                                 ? QStringLiteral("DUCO read status failed")
                                 : state.message);
    }

    applyRobotState(state);
    status_.connectionState = RobotConnectionState::Prepared;
    status_.message = QStringLiteral("DUCO prepared");
    emit statusChanged(status_);
    return {true, status_.message};
}

void DucoRobotController::enqueueTask(const core::RobotTask& task)
{
    if (task.defaultNoop) {
        emit taskAccepted(task);
        emit taskFinished(task, true, QStringLiteral("DUCO default noop finished"));
        return;
    }

    if (status_.connectionState != RobotConnectionState::Prepared) {
        const auto message = QStringLiteral("DUCO robot is not prepared");
        emit warning(message);
        emit taskFinished(task, false, message);
        return;
    }

    const auto recipe = recipes_.value(task.armId);
    auto plan = MotionPlanner::plan(task, recipe);
    if (!plan.ok) {
        const auto message = plan.message;
        emit warning(message);
        emit taskFinished(task, false, message);
        return;
    }

    enqueuePlannedTask(plan.planned);
}

void DucoRobotController::setMotionRecipe(const MotionRecipe& recipe)
{
    recipes_.insert(recipe.armId, recipe);
}

void DucoRobotController::enqueuePlannedTask(const PlannedRobotTask& planned)
{
    const int armId = planned.task.armId;
    arms_[armId].pending.enqueue(planned);
    QTimer::singleShot(0, this, [this, armId]() {
        tryStartNext(armId);
    });
}

void DucoRobotController::tryStartNext(int armId)
{
    auto& state = arms_[armId];
    if (state.busy || state.pending.isEmpty()) {
        return;
    }

    state.busy = true;
    const auto planned = state.pending.dequeue();
    emit taskAccepted(planned.task);

    QString message;
    QStringList warnings;
    DucoMotionWorker worker(motionClient_.get());
    const bool ok = worker.execute(planned, &message, &warnings);
    for (const auto& warningMessage : warnings) {
        emit warning(warningMessage);
    }
    if (!ok) {
        failWithFault(message);
    }
    emit taskFinished(planned.task, ok, message);

    state.busy = false;
    tryStartNext(armId);
}

RobotCommandResult DucoRobotController::stop()
{
    return runControlCommand(QStringLiteral("stop"), &IDucoClient::stop);
}

RobotCommandResult DucoRobotController::pause()
{
    return runControlCommand(QStringLiteral("pause"), &IDucoClient::pause);
}

RobotCommandResult DucoRobotController::resume()
{
    return runControlCommand(QStringLiteral("resume"), &IDucoClient::resume);
}

RobotStatus DucoRobotController::readStatus()
{
    if (!statusClient_) {
        return status_;
    }

    auto state = statusClient_->getRobotState();
    if (!state.ok) {
        failWithFault(state.message.isEmpty()
                          ? QStringLiteral("DUCO read status failed")
                          : state.message);
        return status_;
    }

    applyRobotState(state);
    emit statusChanged(status_);
    return status_;
}

bool DucoRobotController::createRoleClients(QString* errorMessage)
{
    if (!clientFactory_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("DUCO client factory is not configured");
        }
        return false;
    }

    motionClient_ = clientFactory_->createClient(DucoClientRole::Motion);
    controlClient_ = clientFactory_->createClient(DucoClientRole::Control);
    heartbeatClient_ = clientFactory_->createClient(DucoClientRole::Heartbeat);
    statusClient_ = clientFactory_->createClient(DucoClientRole::Status);

    if (!motionClient_ || !controlClient_ || !heartbeatClient_ || !statusClient_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("DUCO client factory returned null role client");
        }
        return false;
    }
    return true;
}

RobotCommandResult DucoRobotController::ensureConnected()
{
    if (status_.connectionState == RobotConnectionState::Connected
        || status_.connectionState == RobotConnectionState::Prepared) {
        return {true, status_.message};
    }
    return connectRobot();
}

RobotCommandResult DucoRobotController::failWithFault(const QString& message)
{
    status_.connectionState = RobotConnectionState::Faulted;
    status_.message = message;
    emit warning(message);
    emit statusChanged(status_);
    return {false, message};
}

RobotCommandResult DucoRobotController::runControlCommand(const QString& command, int (IDucoClient::*method)(bool))
{
    auto connected = ensureConnected();
    if (!connected.ok) {
        return connected;
    }

    if ((controlClient_.get()->*method)(true) == -1) {
        return failWithFault(QStringLiteral("DUCO %1 failed").arg(command));
    }

    const auto message = QStringLiteral("DUCO %1 ok").arg(command);
    status_.message = message;
    emit statusChanged(status_);
    return {true, message};
}

void DucoRobotController::applyRobotState(const DucoRobotState& state)
{
    status_.robotState = state.robotState;
    status_.programState = state.programState;
    status_.safetyState = state.safetyState;
    status_.operationMode = state.operationMode;
    status_.moving = state.moving;
}

RobotCommandResult DucoRobotController::unsupportedCommand(const QString& command) const
{
    return {false, QStringLiteral("DUCO %1 is not implemented").arg(command)};
}

} // namespace spray::robot::duco
