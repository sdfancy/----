#include "test_fake_robot_loop.h"

#include "core/DequeueCoordinator.h"
#include "robot/FakeRobotController.h"

#include <QTcpSocket>
#include <QtTest/QtTest>

using spray::config::PlcConfig;
using spray::core::DequeueCoordinator;
using spray::core::QueueManager;
using spray::domain::QueueItemStatus;
using spray::io::PlcEndpoint;
using spray::protocol::PlcDequeueFrame;
using spray::robot::FakeRobotController;
using spray::robot::IRobotController;
using spray::robot::RobotCommandResult;
using spray::robot::RobotConnectionState;
using spray::robot::RobotStatus;

namespace {

class FailingRobotController final : public IRobotController {
    Q_OBJECT

public:
    using IRobotController::IRobotController;

    RobotCommandResult connectRobot() override { return {true, QStringLiteral("connected")}; }
    void disconnectRobot() override {}
    RobotCommandResult prepare() override { return {true, QStringLiteral("prepared")}; }
    RobotCommandResult stop() override { return {true, QStringLiteral("stopped")}; }
    RobotCommandResult pause() override { return {true, QStringLiteral("paused")}; }
    RobotCommandResult resume() override { return {true, QStringLiteral("resumed")}; }

    RobotStatus readStatus() override
    {
        RobotStatus status;
        status.connectionState = RobotConnectionState::Prepared;
        return status;
    }

    void enqueueTask(const spray::core::RobotTask& task) override
    {
        emit taskAccepted(task);
        emit taskFinished(task, false, QStringLiteral("motion failed"));
    }
};

} // namespace

class FakeRobotLoopTest final : public QObject {
    Q_OBJECT

private slots:
    void sendsAcceptedAndDoneFeedbackInOrder()
    {
        PlcConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.enqueuePort = 19299;
        config.dequeuePort = 19290;

        PlcEndpoint endpoint(config);
        QString error;
        QVERIFY2(endpoint.start(&error), qPrintable(error));

        QueueManager queue;
        queue.enqueuePair(10, 1);

        FakeRobotController robot(1, 1);
        DequeueCoordinator coordinator(&queue, &endpoint, &robot);

        QTcpSocket dequeueSocket;
        dequeueSocket.connectToHost(config.host, config.dequeuePort);
        QVERIFY(dequeueSocket.waitForConnected(1000));
        QTRY_COMPARE(endpoint.dequeueConnectionCount(), 1);

        QSignalSpy feedbackSpy(&coordinator, &DequeueCoordinator::feedbackSent);
        coordinator.onDequeueFrame(PlcDequeueFrame{1, 0});

        QTRY_COMPARE(feedbackSpy.count(), 2);
        QTRY_VERIFY(dequeueSocket.bytesAvailable() >= 4);
        QCOMPARE(dequeueSocket.read(4), QByteArray("1N1D"));

        endpoint.stop();
    }

    void noConnectionOnlyMarksFeedbackFailed()
    {
        PlcConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.enqueuePort = 19399;
        config.dequeuePort = 19390;

        PlcEndpoint endpoint(config);
        QString error;
        QVERIFY2(endpoint.start(&error), qPrintable(error));

        QueueManager queue;
        queue.enqueuePair(10, 1);

        FakeRobotController robot(1, 1);
        DequeueCoordinator coordinator(&queue, &endpoint, &robot);
        QSignalSpy feedbackSpy(&coordinator, &DequeueCoordinator::feedbackSent);

        coordinator.onDequeueFrame(PlcDequeueFrame{1, 0});

        QTRY_COMPARE(feedbackSpy.count(), 2);
        QCOMPARE(feedbackSpy.at(0).at(0).toByteArray(), QByteArray("1N"));
        QCOMPARE(feedbackSpy.at(0).at(1).toBool(), false);
        QCOMPARE(feedbackSpy.at(1).at(0).toByteArray(), QByteArray("1D"));
        QCOMPARE(feedbackSpy.at(1).at(1).toBool(), false);

        endpoint.stop();
    }

    void failedRobotTaskDoesNotSendDoneFeedback()
    {
        PlcConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.enqueuePort = 19499;
        config.dequeuePort = 19490;

        PlcEndpoint endpoint(config);
        QString error;
        QVERIFY2(endpoint.start(&error), qPrintable(error));

        QueueManager queue;
        queue.enqueuePair(10, 1);

        FailingRobotController robot;
        DequeueCoordinator coordinator(&queue, &endpoint, &robot);

        QTcpSocket dequeueSocket;
        dequeueSocket.connectToHost(config.host, config.dequeuePort);
        QVERIFY(dequeueSocket.waitForConnected(1000));
        QTRY_COMPARE(endpoint.dequeueConnectionCount(), 1);

        QSignalSpy feedbackSpy(&coordinator, &DequeueCoordinator::feedbackSent);
        coordinator.onDequeueFrame(PlcDequeueFrame{1, 0});

        QTRY_COMPARE(feedbackSpy.count(), 1);
        QTRY_VERIFY(dequeueSocket.bytesAvailable() >= 2);
        QCOMPARE(dequeueSocket.read(2), QByteArray("1N"));
        QCOMPARE(dequeueSocket.bytesAvailable(), qint64(0));

        const auto item = queue.itemFor(1, 1);
        QVERIFY(item.has_value());
        QCOMPARE(item->status, QueueItemStatus::Accepted);
        QVERIFY(!item->confirmed);
        QVERIFY(queue.cacheFor(1).has_value());

        endpoint.stop();
    }
};

QObject* createFakeRobotLoopTest()
{
    return new FakeRobotLoopTest();
}

#include "test_fake_robot_loop.moc"
