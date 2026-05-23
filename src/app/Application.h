#pragma once

#include "config/AppConfig.h"
#include "core/DequeueCoordinator.h"
#include "core/EnqueueWorkflow.h"
#include "core/LegacyCameraWorkflow.h"
#include "core/QueueManager.h"
#include "diagnostics/EventLog.h"
#include "io/camera/CameraEndpoint.h"
#include "io/plc/PlcEndpoint.h"
#include "robot/FakeRobotController.h"

#include <QObject>
#include <memory>

namespace spray::app {

class Application final : public QObject {
    Q_OBJECT

public:
    Application(config::AppConfig config, bool simulateRobot, QObject* parent = nullptr);

    bool initialize(QString* errorMessage = nullptr);
    bool start(QString* errorMessage = nullptr);
    void stop();
    domain::QueueSnapshot queueSnapshot() const;
    QList<diagnostics::EventRecord> events() const;

private:
    void wireEvents();
    void wireDiagnosticEvents();
    void wirePlcEnqueueEvents();
    void wirePlcDequeueEvents();
    void wireDequeueCoordinatorEvents();
    void wireCameraEvents();
    void wireWorkflowEvents();
    void handlePlcEnqueueFrame(const QByteArray& bytes);

    config::AppConfig config_;
    bool simulateRobot_ = false;
    std::unique_ptr<core::QueueManager> queueManager_;
    std::unique_ptr<io::PlcEndpoint> plcEndpoint_;
    std::unique_ptr<io::CameraEndpoint> cameraEndpoint_;
    std::unique_ptr<robot::FakeRobotController> fakeRobot_;
    std::unique_ptr<core::DequeueCoordinator> dequeueCoordinator_;
    std::unique_ptr<core::EnqueueWorkflow> enqueueWorkflow_;
    std::unique_ptr<core::LegacyCameraWorkflow> legacyCameraWorkflow_;
    diagnostics::EventLog eventLog_;
};

} // namespace spray::app
