#pragma once

#include "core/QueueManager.h"

#include <QObject>
#include <QString>

namespace spray::robot {

enum class RobotConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Prepared,
    Faulted,
};

struct RobotStatus {
    RobotConnectionState connectionState = RobotConnectionState::Disconnected;
    bool moving = false;
    int robotState = 0;
    int programState = 0;
    int safetyState = 0;
    int operationMode = 0;
    QString message;
};

struct RobotCommandResult {
    bool ok = false;
    QString message;
};

class IRobotController : public QObject {
    Q_OBJECT

public:
    explicit IRobotController(QObject* parent = nullptr);
    ~IRobotController() override;

    virtual RobotCommandResult connectRobot() = 0;
    virtual void disconnectRobot() = 0;
    virtual RobotCommandResult prepare() = 0;
    virtual void enqueueTask(const core::RobotTask& task) = 0;
    virtual RobotCommandResult stop() = 0;
    virtual RobotCommandResult pause() = 0;
    virtual RobotCommandResult resume() = 0;
    virtual RobotStatus readStatus() = 0;

signals:
    void taskAccepted(spray::core::RobotTask task);
    void taskFinished(spray::core::RobotTask task, bool ok, QString message);
    void statusChanged(spray::robot::RobotStatus status);
    void warning(QString message);
};

} // namespace spray::robot
