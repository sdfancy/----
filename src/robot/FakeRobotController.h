#pragma once

#include "core/QueueManager.h"

#include <QObject>
#include <QQueue>
#include <QSet>

namespace spray::robot {

class FakeRobotController final : public QObject {
    Q_OBJECT

public:
    explicit FakeRobotController(int acceptDelayMs = 20, int finishDelayMs = 80, QObject* parent = nullptr);

    void enqueueTask(const core::RobotTask& task);
    int pendingCount(int armId) const;

signals:
    void taskAccepted(spray::core::RobotTask task);
    void taskFinished(spray::core::RobotTask task, bool ok, QString message);

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
