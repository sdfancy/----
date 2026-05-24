#pragma once

#include "config/AppConfig.h"
#include "robot/IRobotController.h"
#include "robot/MotionTypes.h"
#include "robot/duco/DucoClient.h"

#include <QHash>
#include <QQueue>

#include <memory>

namespace spray::robot::duco {

class DucoRobotController final : public IRobotController {
    Q_OBJECT

public:
    DucoRobotController(config::RobotConfig config,
                        std::unique_ptr<IDucoClientFactory> clientFactory,
                        QObject* parent = nullptr);

    RobotCommandResult connectRobot() override;
    void disconnectRobot() override;
    RobotCommandResult prepare() override;
    void enqueueTask(const core::RobotTask& task) override;
    RobotCommandResult stop() override;
    RobotCommandResult pause() override;
    RobotCommandResult resume() override;
    RobotStatus readStatus() override;
    void setMotionRecipe(const MotionRecipe& recipe);

private:
    struct ArmExecutionState {
        bool busy = false;
        QQueue<PlannedRobotTask> pending;
    };

    bool createRoleClients(QString* errorMessage);
    RobotCommandResult ensureConnected();
    RobotCommandResult failWithFault(const QString& message);
    RobotCommandResult runControlCommand(const QString& command, int (IDucoClient::*method)(bool));
    void applyRobotState(const DucoRobotState& state);
    RobotCommandResult unsupportedCommand(const QString& command) const;
    void enqueuePlannedTask(const PlannedRobotTask& planned);
    void tryStartNext(int armId);

    config::RobotConfig config_;
    std::unique_ptr<IDucoClientFactory> clientFactory_;
    std::unique_ptr<IDucoClient> motionClient_;
    std::unique_ptr<IDucoClient> controlClient_;
    std::unique_ptr<IDucoClient> heartbeatClient_;
    std::unique_ptr<IDucoClient> statusClient_;
    RobotStatus status_;
    QHash<int, MotionRecipe> recipes_;
    QHash<int, ArmExecutionState> arms_;
};

} // namespace spray::robot::duco
