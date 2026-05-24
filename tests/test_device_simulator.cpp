#include "test_device_simulator.h"

#include "app/Application.h"
#include "diagnostics/DeviceSimulator.h"

#include <QtTest/QtTest>

using spray::app::Application;
using spray::config::AppConfig;
using spray::diagnostics::DeviceSimulator;
using spray::diagnostics::EventFilter;

class DeviceSimulatorTest final : public QObject {
    Q_OBJECT

private slots:
    void runsMinimalPlcLoopThroughPublicPorts()
    {
        AppConfig config;
        config.plc.host = QStringLiteral("127.0.0.1");
        config.plc.enqueuePort = 19899;
        config.plc.dequeuePort = 19890;
        config.fakeRobot.acceptDelayMs = 1;
        config.fakeRobot.finishDelayMs = 1;

        Application app(config, true);
        QString error;
        QVERIFY2(app.initialize(&error), qPrintable(error));
        QVERIFY2(app.start(&error), qPrintable(error));

        DeviceSimulator simulator;
        QByteArray feedback;
        QVERIFY2(simulator.runPlcMinimalLoop(config.plc, 10, 1, &feedback, &error), qPrintable(error));
        QCOMPARE(feedback, QByteArray("1N1D"));

        EventFilter filter;
        filter.device = QStringLiteral("plc.enqueue");
        filter.count = 10;
        QTRY_VERIFY(!app.events(filter).isEmpty());

        bool sawFeedback = false;
        for (const auto& event : app.events()) {
            if (event.category == QStringLiteral("plc.feedback")
                && event.message.contains(QStringLiteral("1D"))) {
                sawFeedback = true;
            }
        }
        QVERIFY(sawFeedback);
        app.stop();
    }

    void runsDualCameraCycleThroughPublicPorts()
    {
        AppConfig config;
        config.plc.host = QStringLiteral("127.0.0.1");
        config.plc.enqueuePort = 19999;
        config.plc.dequeuePort = 19990;
        config.camera.host = QStringLiteral("127.0.0.1");
        config.camera.camera2dPort = 19991;
        config.camera.camera3dPort = 19992;
        config.camera.camera3dEnabled = true;
        config.camera.flowMode = spray::config::CameraFlowMode::DualCamera11_12;
        config.fakeRobot.acceptDelayMs = 1;
        config.fakeRobot.finishDelayMs = 1;

        Application app(config, true);
        QString error;
        QVERIFY2(app.initialize(&error), qPrintable(error));
        QVERIFY2(app.start(&error), qPrintable(error));

        DeviceSimulator simulator;
        QByteArray feedback;
        QVERIFY2(simulator.runDualCameraCycle(config.plc, config.camera, 10, 1, &feedback, &error),
                 qPrintable(error));
        QCOMPARE(feedback, QByteArray("Done"));

        QTRY_VERIFY(app.queueSnapshot().items.size() >= 2);
        bool sawCameraPayload = false;
        for (const auto& item : app.queueSnapshot().items) {
            if (item.count == 10 && item.pointer == 1 && item.source == QStringLiteral("camera")) {
                sawCameraPayload = true;
            }
        }
        QVERIFY(sawCameraPayload);

        bool sawDone = false;
        for (const auto& event : app.events()) {
            if (event.category == QStringLiteral("plc.enqueue.feedback")
                && event.message.contains(QStringLiteral("Done"))) {
                sawDone = true;
            }
        }
        QVERIFY(sawDone);
        app.stop();
    }
};

QObject* createDeviceSimulatorTest()
{
    return new DeviceSimulatorTest();
}

#include "test_device_simulator.moc"
