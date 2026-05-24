#include "test_app_config.h"
#include "test_application_loop.h"
#include "test_camera_endpoint.h"
#include "test_camera_protocol.h"
#include "test_duco_robot_controller.h"
#include "test_diagnostics_event_log.h"
#include "test_diagnostics_service.h"
#include "test_device_simulator.h"
#include "test_enqueue_workflow.h"
#include "test_fake_robot_loop.h"
#include "test_legacy_camera_workflow.h"
#include "test_modbus_address_table.h"
#include "test_motion_planner.h"
#include "test_plc_modbus_client.h"
#include "test_plc_endpoint.h"
#include "test_plc_protocol.h"
#include "test_queue_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QtTest/QtTest>

namespace {

int runTest(QObject* test, const QString& outputName)
{
    const QByteArray outputOption = QStringLiteral("-o").toLocal8Bit();
    const QByteArray outputTarget = (QStringLiteral("test-output/") + outputName + QStringLiteral(",txt")).toLocal8Bit();
    char arg0[] = "spray_tests";
    char* argv[] = {arg0, const_cast<char*>(outputOption.constData()), const_cast<char*>(outputTarget.constData())};
    int argc = 3;
    const int status = QTest::qExec(test, argc, argv);
    delete test;
    return status;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QDir().mkpath(QStringLiteral("test-output"));
    int status = 0;

    status |= runTest(createAppConfigTest(), QStringLiteral("app-config.txt"));
    status |= runTest(createApplicationLoopTest(), QStringLiteral("application-loop.txt"));
    status |= runTest(createCameraEndpointTest(), QStringLiteral("camera-endpoint.txt"));
    status |= runTest(createCameraProtocolTest(), QStringLiteral("camera-protocol.txt"));
    status |= runTest(createDiagnosticsEventLogTest(), QStringLiteral("diagnostics-event-log.txt"));
    status |= runTest(createDiagnosticsServiceTest(), QStringLiteral("diagnostics-service.txt"));
    status |= runTest(createDeviceSimulatorTest(), QStringLiteral("device-simulator.txt"));
    status |= runTest(createDucoRobotControllerTest(), QStringLiteral("duco-robot-controller.txt"));
    status |= runTest(createEnqueueWorkflowTest(), QStringLiteral("enqueue-workflow.txt"));
    status |= runTest(createFakeRobotLoopTest(), QStringLiteral("fake-robot-loop.txt"));
    status |= runTest(createLegacyCameraWorkflowTest(), QStringLiteral("legacy-camera-workflow.txt"));
    status |= runTest(createModbusAddressTableTest(), QStringLiteral("modbus-address-table.txt"));
    status |= runTest(createMotionPlannerTest(), QStringLiteral("motion-planner.txt"));
    status |= runTest(createPlcModbusClientTest(), QStringLiteral("plc-modbus-client.txt"));
    status |= runTest(createPlcEndpointTest(), QStringLiteral("plc-endpoint.txt"));
    status |= runTest(createPlcProtocolTest(), QStringLiteral("plc-protocol.txt"));
    status |= runTest(createQueueManagerTest(), QStringLiteral("queue-manager.txt"));

    return status;
}
