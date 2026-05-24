#include "test_duco_robot_controller.h"

#include "robot/duco/DucoRobotController.h"

#include <QHash>
#include <QSharedPointer>
#include <QtTest/QtTest>

using spray::config::RobotConfig;
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
};

QObject* createDucoRobotControllerTest()
{
    return new DucoRobotControllerTest();
}

#include "test_duco_robot_controller.moc"
