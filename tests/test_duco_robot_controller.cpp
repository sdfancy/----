#include "test_duco_robot_controller.h"

#include "robot/duco/DucoRobotController.h"

#include <QHash>
#include <QSharedPointer>
#include <QtTest/QtTest>

using spray::config::RobotConfig;
using spray::core::RobotTask;
using spray::robot::Joint6d;
using spray::robot::MotionRecipe;
using spray::robot::MotionSegmentType;
using spray::robot::Pose6d;
using spray::robot::duco::DucoClientRole;
using spray::robot::duco::DucoRobotController;
using spray::robot::duco::DucoRobotState;
using spray::robot::duco::IDucoClient;
using spray::robot::duco::IDucoClientFactory;
using spray::robot::duco::roleName;

namespace {

struct RecordingState {
    int nextId = 1;
    QHash<QString, int> roleIds;
    QStringList calls;
    QHash<QString, int> returnByCall;
    DucoRobotState robotState;
};

class RecordingDucoClient final : public IDucoClient {
public:
    RecordingDucoClient(DucoClientRole role, QSharedPointer<RecordingState> state)
        : role_(role)
        , state_(std::move(state))
        , id_(state_->nextId++)
    {
        state_->roleIds.insert(roleName(role_), id_);
    }

    int open() override
    {
        state_->calls.append(roleName(role_) + QStringLiteral(".open"));
        return 0;
    }

    int close() override
    {
        state_->calls.append(roleName(role_) + QStringLiteral(".close"));
        return 0;
    }

    void rpcHeartbeat(int timeMs) override
    {
        state_->calls.append(roleName(role_) + QStringLiteral(".heartbeat:%1").arg(timeMs));
    }

    int powerOn(bool block) override
    {
        return recordInt(QStringLiteral("powerOn:%1").arg(block));
    }

    int enable(bool block) override
    {
        return recordInt(QStringLiteral("enable:%1").arg(block));
    }

    int stop(bool block) override
    {
        return recordInt(QStringLiteral("stop:%1").arg(block));
    }

    int pause(bool block) override
    {
        return recordInt(QStringLiteral("pause:%1").arg(block));
    }

    int resume(bool block) override
    {
        return recordInt(QStringLiteral("resume:%1").arg(block));
    }

    DucoRobotState getRobotState() override
    {
        state_->calls.append(roleName(role_) + QStringLiteral(".getRobotState"));
        return state_->robotState;
    }

    int moveJPose2(const Pose6d& pose,
                   double velocity,
                   double acceleration,
                   double radius,
                   const Joint6d& qNear,
                   const QString& tool,
                   const QString& wobj,
                   bool block) override
    {
        Q_UNUSED(pose)
        Q_UNUSED(qNear)
        Q_UNUSED(tool)
        Q_UNUSED(wobj)
        return recordInt(QStringLiteral("moveJPose2:%1:%2:%3:%4")
                             .arg(velocity)
                             .arg(acceleration)
                             .arg(radius)
                             .arg(block));
    }

    int moveL(const Pose6d& pose,
              double velocity,
              double acceleration,
              double radius,
              const Joint6d& qNear,
              const QString& tool,
              const QString& wobj,
              bool block) override
    {
        Q_UNUSED(pose)
        Q_UNUSED(qNear)
        Q_UNUSED(tool)
        Q_UNUSED(wobj)
        return recordInt(QStringLiteral("moveL:%1:%2:%3:%4")
                             .arg(velocity)
                             .arg(acceleration)
                             .arg(radius)
                             .arg(block));
    }

    int setToolDigitalOut(int channel, bool value, bool block) override
    {
        return recordInt(QStringLiteral("setToolDigitalOut:%1:%2:%3").arg(channel).arg(value).arg(block));
    }

    int setStandardDigitalOut(int channel, bool value, bool block) override
    {
        return recordInt(QStringLiteral("setStandardDigitalOut:%1:%2:%3").arg(channel).arg(value).arg(block));
    }

private:
    int recordInt(const QString& call)
    {
        const QString key = roleName(role_) + QStringLiteral(".") + call;
        state_->calls.append(key);
        return state_->returnByCall.value(key, 0);
    }

    DucoClientRole role_;
    QSharedPointer<RecordingState> state_;
    int id_ = 0;
};

class RecordingFactory final : public IDucoClientFactory {
public:
    explicit RecordingFactory(QSharedPointer<RecordingState> state)
        : state_(std::move(state))
    {
    }

    std::unique_ptr<IDucoClient> createClient(DucoClientRole role) override
    {
        return std::make_unique<RecordingDucoClient>(role, state_);
    }

private:
    QSharedPointer<RecordingState> state_;
};

MotionRecipe recipeForArm(int armId)
{
    MotionRecipe recipe;
    recipe.enabled = true;
    recipe.armId = armId;
    recipe.tool = QStringLiteral("spray_tool");
    recipe.wobj = QStringLiteral("station");
    recipe.approachSpeed = 0.5;
    recipe.lineSpeed = 0.25;
    recipe.acceleration = 0.8;
    recipe.radius = 0.01;
    recipe.sprayIoType = MotionSegmentType::ToolDigitalOut;
    recipe.sprayIoChannel = 2;
    return recipe;
}

RobotTask robotTask(int armId, quint16 count, const QByteArray& payload, bool defaultNoop = false)
{
    RobotTask task;
    task.armId = armId;
    task.count = count;
    task.pointer = 9;
    task.payload = payload;
    task.defaultNoop = defaultNoop;
    return task;
}

} // namespace

class DucoRobotControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void createsSeparateClientsForEachRole()
    {
        auto state = QSharedPointer<RecordingState>::create();
        RobotConfig config;
        DucoRobotController controller(config, std::make_unique<RecordingFactory>(state));

        const auto result = controller.connectRobot();

        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(state->roleIds.size(), 4);
        QVERIFY(state->roleIds.contains(QStringLiteral("motion")));
        QVERIFY(state->roleIds.contains(QStringLiteral("control")));
        QVERIFY(state->roleIds.contains(QStringLiteral("heartbeat")));
        QVERIFY(state->roleIds.contains(QStringLiteral("status")));
        QCOMPARE(QSet<int>(state->roleIds.cbegin(), state->roleIds.cend()).size(), 4);
    }

    void prepareRunsExpectedDucoSequence()
    {
        auto state = QSharedPointer<RecordingState>::create();
        state->robotState.robotState = 1;
        state->robotState.programState = 2;
        state->robotState.safetyState = 3;
        state->robotState.operationMode = 4;
        state->robotState.moving = true;
        RobotConfig config;
        config.heartbeatMs = 250;
        DucoRobotController controller(config, std::make_unique<RecordingFactory>(state));

        const auto result = controller.prepare();

        QVERIFY2(result.ok, qPrintable(result.message));
        const QStringList expected{
            QStringLiteral("motion.open"),
            QStringLiteral("control.open"),
            QStringLiteral("heartbeat.open"),
            QStringLiteral("status.open"),
            QStringLiteral("heartbeat.heartbeat:250"),
            QStringLiteral("control.powerOn:1"),
            QStringLiteral("control.enable:1"),
            QStringLiteral("status.getRobotState"),
        };
        QCOMPARE(state->calls, expected);

        const auto status = controller.readStatus();
        QCOMPARE(status.connectionState, spray::robot::RobotConnectionState::Prepared);
        QVERIFY(status.moving);
        QCOMPARE(status.robotState, 1);
        QCOMPARE(status.programState, 2);
        QCOMPARE(status.safetyState, 3);
        QCOMPARE(status.operationMode, 4);
    }

    void prepareFaultsOnNegativeReturn()
    {
        auto state = QSharedPointer<RecordingState>::create();
        state->returnByCall.insert(QStringLiteral("control.powerOn:1"), -1);
        RobotConfig config;
        DucoRobotController controller(config, std::make_unique<RecordingFactory>(state));
        QSignalSpy warningSpy(&controller, &DucoRobotController::warning);

        const auto result = controller.prepare();

        QVERIFY(!result.ok);
        QCOMPARE(result.message, QStringLiteral("DUCO power_on failed"));
        QCOMPARE(controller.readStatus().connectionState, spray::robot::RobotConnectionState::Faulted);
        QCOMPARE(warningSpy.count(), 1);
    }

    void taskControlUsesControlClient()
    {
        auto state = QSharedPointer<RecordingState>::create();
        RobotConfig config;
        DucoRobotController controller(config, std::make_unique<RecordingFactory>(state));

        QVERIFY(controller.stop().ok);
        QVERIFY(controller.pause().ok);
        QVERIFY(controller.resume().ok);

        QVERIFY(state->calls.contains(QStringLiteral("control.stop:1")));
        QVERIFY(state->calls.contains(QStringLiteral("control.pause:1")));
        QVERIFY(state->calls.contains(QStringLiteral("control.resume:1")));
        QVERIFY(!state->calls.contains(QStringLiteral("motion.stop:1")));
        QVERIFY(!state->calls.contains(QStringLiteral("heartbeat.pause:1")));
    }

    void readStatusUsesStatusClient()
    {
        auto state = QSharedPointer<RecordingState>::create();
        state->robotState.robotState = 7;
        RobotConfig config;
        DucoRobotController controller(config, std::make_unique<RecordingFactory>(state));
        QVERIFY(controller.connectRobot().ok);

        const auto status = controller.readStatus();

        QCOMPARE(status.robotState, 7);
        QVERIFY(state->calls.contains(QStringLiteral("status.getRobotState")));
        QVERIFY(!state->calls.contains(QStringLiteral("control.getRobotState")));
    }

    void recordingClientCapturesMotionAndIoCalls()
    {
        auto state = QSharedPointer<RecordingState>::create();
        RecordingDucoClient client(DucoClientRole::Motion, state);

        QVERIFY(client.moveJPose2(Pose6d{}, 0.5, 0.8, 0.01, Joint6d{}, QStringLiteral("tool"),
                                  QStringLiteral("wobj"), true) == 0);
        QVERIFY(client.setToolDigitalOut(2, true, true) == 0);
        QVERIFY(client.moveL(Pose6d{}, 0.25, 0.8, 0.01, Joint6d{}, QStringLiteral("tool"),
                             QStringLiteral("wobj"), true) == 0);
        QVERIFY(client.setStandardDigitalOut(3, false, true) == 0);

        QVERIFY(state->calls.contains(QStringLiteral("motion.moveJPose2:0.5:0.8:0.01:1")));
        QVERIFY(state->calls.contains(QStringLiteral("motion.setToolDigitalOut:2:1:1")));
        QVERIFY(state->calls.contains(QStringLiteral("motion.moveL:0.25:0.8:0.01:1")));
        QVERIFY(state->calls.contains(QStringLiteral("motion.setStandardDigitalOut:3:0:1")));
    }

    void defaultNoopDoesNotCallMotionClient()
    {
        auto state = QSharedPointer<RecordingState>::create();
        RobotConfig config;
        DucoRobotController controller(config, std::make_unique<RecordingFactory>(state));
        int acceptedCount = 0;
        bool finishedOk = false;
        connect(&controller, &DucoRobotController::taskAccepted, this,
                [&acceptedCount](const RobotTask&) { ++acceptedCount; });
        connect(&controller, &DucoRobotController::taskFinished, this,
                [&finishedOk](const RobotTask&, bool ok, const QString&) { finishedOk = ok; });

        controller.enqueueTask(robotTask(1, 0, QByteArray("(1000,0)E"), true));

        QCOMPARE(acceptedCount, 1);
        QVERIFY(finishedOk);
        QVERIFY(state->calls.isEmpty());
    }

    void validTaskExecutesPlannedMotionSequence()
    {
        auto state = QSharedPointer<RecordingState>::create();
        RobotConfig config;
        DucoRobotController controller(config, std::make_unique<RecordingFactory>(state));
        QVERIFY(controller.prepare().ok);
        controller.setMotionRecipe(recipeForArm(1));
        state->calls.clear();

        int acceptedCount = 0;
        bool finishedOk = false;
        connect(&controller, &DucoRobotController::taskAccepted, this,
                [&acceptedCount](const RobotTask&) { ++acceptedCount; });
        connect(&controller, &DucoRobotController::taskFinished, this,
                [&finishedOk](const RobotTask&, bool ok, const QString&) { finishedOk = ok; });

        controller.enqueueTask(robotTask(
            1, 123, QByteArray("(1001,123,0.49,0.14,0.44,-1.14,0,-1.57)E")));

        QTRY_COMPARE(acceptedCount, 1);
        QTRY_VERIFY(finishedOk);
        const QStringList expected{
            QStringLiteral("motion.moveJPose2:0.5:0.8:0.01:1"),
            QStringLiteral("motion.setToolDigitalOut:2:1:1"),
            QStringLiteral("motion.moveL:0.25:0.8:0.01:1"),
            QStringLiteral("motion.setToolDigitalOut:2:0:1"),
        };
        QCOMPARE(state->calls, expected);
    }

    void missingRecipeFailsWithoutAcceptedOrMotion()
    {
        auto state = QSharedPointer<RecordingState>::create();
        RobotConfig config;
        DucoRobotController controller(config, std::make_unique<RecordingFactory>(state));
        QVERIFY(controller.prepare().ok);
        state->calls.clear();
        int acceptedCount = 0;
        bool finishedCalled = false;
        bool finishedOk = true;
        connect(&controller, &DucoRobotController::taskAccepted, this,
                [&acceptedCount](const RobotTask&) { ++acceptedCount; });
        connect(&controller, &DucoRobotController::taskFinished, this,
                [&finishedCalled, &finishedOk](const RobotTask&, bool ok, const QString&) {
                    finishedCalled = true;
                    finishedOk = ok;
                });

        controller.enqueueTask(robotTask(
            1, 123, QByteArray("(1001,123,0.49,0.14,0.44,-1.14,0,-1.57)E")));

        QCOMPARE(acceptedCount, 0);
        QVERIFY(finishedCalled);
        QVERIFY(!finishedOk);
        QVERIFY(state->calls.isEmpty());
    }

    void motionFailureClosesSprayIoAndFaults()
    {
        auto state = QSharedPointer<RecordingState>::create();
        state->returnByCall.insert(QStringLiteral("motion.moveL:0.25:0.8:0.01:1"), -1);
        RobotConfig config;
        DucoRobotController controller(config, std::make_unique<RecordingFactory>(state));
        QVERIFY(controller.prepare().ok);
        controller.setMotionRecipe(recipeForArm(1));
        state->calls.clear();
        bool finishedOk = true;
        connect(&controller, &DucoRobotController::taskFinished, this,
                [&finishedOk](const RobotTask&, bool ok, const QString&) { finishedOk = ok; });

        controller.enqueueTask(robotTask(
            1, 123, QByteArray("(1001,123,0.49,0.14,0.44,-1.14,0,-1.57)E")));

        QTRY_VERIFY(!finishedOk);
        QTRY_VERIFY(state->calls.contains(QStringLiteral("motion.setToolDigitalOut:2:0:1")));
        QTRY_COMPARE(controller.readStatus().connectionState, spray::robot::RobotConnectionState::Faulted);
    }
};

QObject* createDucoRobotControllerTest()
{
    return new DucoRobotControllerTest();
}

#include "test_duco_robot_controller.moc"
