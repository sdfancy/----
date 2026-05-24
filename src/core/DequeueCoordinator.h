#pragma once

#include "core/QueueManager.h"
#include "io/plc/PlcEndpoint.h"
#include "robot/IRobotController.h"

#include <QObject>

namespace spray::core {

class DequeueCoordinator final : public QObject {
    Q_OBJECT

public:
    DequeueCoordinator(QueueManager* queueManager,
                       io::PlcEndpoint* plcEndpoint,
                       robot::IRobotController* robot,
                       QObject* parent = nullptr);

public slots:
    void onDequeueFrame(protocol::PlcDequeueFrame frame);
    void onTaskAccepted(core::RobotTask task);
    void onTaskFinished(core::RobotTask task, bool ok, QString message);

signals:
    void taskDispatched(spray::core::RobotTask task);
    void feedbackSent(QByteArray feedback, bool ok);

private:
    QueueManager* queueManager_ = nullptr;
    io::PlcEndpoint* plcEndpoint_ = nullptr;
    robot::IRobotController* robot_ = nullptr;
};

} // namespace spray::core
