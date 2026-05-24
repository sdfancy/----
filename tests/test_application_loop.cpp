#include "test_application_loop.h"

#include "app/Application.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QtTest/QtTest>

using spray::app::Application;
using spray::config::AppConfig;

namespace {

QString writeValidRecipes(QTemporaryDir& dir)
{
    const QString path = dir.filePath(QStringLiteral("motion_recipes.toml"));
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Text);
    Q_ASSERT(opened);
    file.write(R"toml(
[[recipes]]
arm_id = 1
enabled = true
q_near = [0, 0, 0, 0, 0, 0]
pose_indices = [0, 1, 2, 3, 4, 5]
approach_speed = 0.5
line_speed = 0.25
acceleration = 0.8
spray_io = "tool"
spray_io_channel = 2

[[recipes]]
arm_id = 2
enabled = true
q_near = [0, 0, 0, 0, 0, 0]
pose_indices = [0, 1, 2, 3, 4, 5]
approach_speed = 0.5
line_speed = 0.25
acceleration = 0.8
spray_io = "tool"
spray_io_channel = 3
)toml");
    return path;
}

} // namespace

class ApplicationLoopTest final : public QObject {
    Q_OBJECT

private slots:
    void tracksLoopInEventsAndSnapshot()
    {
        AppConfig config;
        config.plc.host = QStringLiteral("127.0.0.1");
        config.plc.enqueuePort = 19499;
        config.plc.dequeuePort = 19490;
        config.fakeRobot.acceptDelayMs = 1;
        config.fakeRobot.finishDelayMs = 1;

        Application app(config, true);
        QString error;
        QVERIFY2(app.initialize(&error), qPrintable(error));
        QVERIFY2(app.start(&error), qPrintable(error));

        QTcpSocket enqueueSocket;
        enqueueSocket.connectToHost(config.plc.host, config.plc.enqueuePort);
        QVERIFY(enqueueSocket.waitForConnected(1000));
        enqueueSocket.write(QByteArray::fromHex("0001000A0001"));
        QVERIFY(enqueueSocket.waitForBytesWritten(1000));

        QTcpSocket dequeueSocket;
        dequeueSocket.connectToHost(config.plc.host, config.plc.dequeuePort);
        QVERIFY(dequeueSocket.waitForConnected(1000));
        dequeueSocket.write(QByteArray::fromHex("00010000"));
        QVERIFY(dequeueSocket.waitForBytesWritten(1000));

        QTRY_VERIFY(dequeueSocket.bytesAvailable() >= 4);
        QCOMPARE(dequeueSocket.read(4), QByteArray("1N1D"));

        QTRY_VERIFY(app.events().size() >= 7);
        const auto snapshot = app.queueSnapshot();
        QVERIFY(!snapshot.items.isEmpty());

        bool sawPointer = false;
        for (const auto& item : snapshot.items) {
            if (item.armId == 1 && item.pointer == 1 && item.count == 10) {
                sawPointer = true;
            }
        }
        QVERIFY(sawPointer);

        app.stop();
    }

    void runsDualCameraEnqueueFlow()
    {
        AppConfig config;
        config.plc.host = QStringLiteral("127.0.0.1");
        config.plc.enqueuePort = 19699;
        config.plc.dequeuePort = 19690;
        config.camera.host = QStringLiteral("127.0.0.1");
        config.camera.camera2dPort = 19691;
        config.camera.camera3dPort = 19692;
        config.camera.camera3dEnabled = true;
        config.camera.flowMode = spray::config::CameraFlowMode::DualCamera11_12;
        config.fakeRobot.acceptDelayMs = 1;
        config.fakeRobot.finishDelayMs = 1;

        Application app(config, true);
        QString error;
        QVERIFY2(app.initialize(&error), qPrintable(error));
        QVERIFY2(app.start(&error), qPrintable(error));

        QTcpSocket plcEnqueue;
        plcEnqueue.connectToHost(config.plc.host, config.plc.enqueuePort);
        QVERIFY(plcEnqueue.waitForConnected(1000));

        QTcpSocket camera2d;
        QTcpSocket camera3d;
        camera2d.connectToHost(config.camera.host, config.camera.camera2dPort);
        camera3d.connectToHost(config.camera.host, config.camera.camera3dPort);
        QVERIFY(camera2d.waitForConnected(1000));
        QVERIFY(camera3d.waitForConnected(1000));
        camera2d.write(QByteArray("READY"));
        camera3d.write(QByteArray("probe"));
        QVERIFY(camera2d.waitForBytesWritten(1000));
        QVERIFY(camera3d.waitForBytesWritten(1000));
        QTRY_VERIFY([&app]() {
            int cameraRawEvents = 0;
            for (const auto& event : app.events()) {
                if (event.category == QStringLiteral("camera.raw")) {
                    ++cameraRawEvents;
                }
            }
            return cameraRawEvents >= 2;
        }());

        plcEnqueue.write(QByteArray::fromHex("000B000A0001"));
        QVERIFY(plcEnqueue.waitForBytesWritten(1000));
        QTRY_VERIFY(camera3d.bytesAvailable() > 0);
        QCOMPARE(camera3d.readAll(), QByteArray("11,10"));

        camera2d.write(QByteArray("READY"));
        QVERIFY(camera2d.waitForBytesWritten(1000));
        QTRY_VERIFY(camera2d.bytesAvailable() > 0);
        QCOMPARE(camera2d.readAll(), QByteArray("(10)E"));

        camera2d.write(QByteArray("10,01,02"));
        QVERIFY(camera2d.waitForBytesWritten(1000));
        QTRY_VERIFY(camera2d.bytesAvailable() > 0);
        QCOMPARE(camera2d.readAll(), QByteArray("OK"));

        plcEnqueue.write(QByteArray::fromHex("000C000A0001"));
        QVERIFY(plcEnqueue.waitForBytesWritten(1000));
        QTRY_VERIFY(camera3d.bytesAvailable() > 0);
        QCOMPARE(camera3d.readAll(), QByteArray("12,10,01,02"));

        camera3d.write(QByteArray("(1001,10,a)EA(2001,10,b)E"));
        QVERIFY(camera3d.waitForBytesWritten(1000));
        QTRY_VERIFY(plcEnqueue.bytesAvailable() > 0);
        QCOMPARE(plcEnqueue.readAll(), QByteArray("Done"));

        QTRY_VERIFY(app.queueSnapshot().items.size() >= 2);
        bool sawArm1Camera = false;
        bool sawArm2Camera = false;
        for (const auto& item : app.queueSnapshot().items) {
            if (item.armId == 1 && item.pointer == 1 && item.count == 10 && item.source == QStringLiteral("camera")) {
                sawArm1Camera = true;
            }
            if (item.armId == 2 && item.pointer == 1 && item.count == 10 && item.source == QStringLiteral("camera")) {
                sawArm2Camera = true;
            }
        }
        QVERIFY(sawArm1Camera);
        QVERIFY(sawArm2Camera);

        bool sawPlcCount = false;
        bool sawFeedback = false;
        for (const auto& event : app.events()) {
            if (event.category == QStringLiteral("queue.enqueue") && event.message.contains(QStringLiteral("count=10"))) {
                sawPlcCount = true;
            }
            if (event.category == QStringLiteral("plc.enqueue.feedback")
                && event.message.contains(QStringLiteral("Done"))) {
                sawFeedback = true;
            }
        }
        QVERIFY(sawPlcCount);
        QVERIFY(sawFeedback);

        app.stop();
    }

    void ducoModeWithoutSdkFailsStart()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppConfig config;
        config.robot.mode = spray::config::RobotMode::Duco;
        config.robot.recipePath = writeValidRecipes(dir);

        Application app(config, false);
        QString error;

        QVERIFY2(app.initialize(&error), qPrintable(error));
        QVERIFY(!app.start(&error));
        QCOMPARE(error, QStringLiteral("DUCO open failed"));
    }

    void ducoModeRejectsMissingRecipeFile()
    {
        AppConfig config;
        config.robot.mode = spray::config::RobotMode::Duco;
        config.robot.recipePath = QStringLiteral("missing/motion_recipes.toml");

        Application app(config, false);
        QString error;

        QVERIFY(!app.initialize(&error));
        QVERIFY(error.contains(QStringLiteral("cannot open motion recipe table")));
    }
};

QObject* createApplicationLoopTest()
{
    return new ApplicationLoopTest();
}

#include "test_application_loop.moc"
