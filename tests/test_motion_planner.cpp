#include "test_motion_planner.h"

#include "robot/MotionPlanner.h"

#include <QtTest/QtTest>

using spray::core::RobotTask;
using spray::robot::MotionRecipe;
using spray::robot::MotionSegmentType;
using spray::robot::MotionPlanner;

namespace {

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

RobotTask taskForPayload(int armId, quint16 count, const QByteArray& payload, bool defaultNoop = false)
{
    RobotTask task;
    task.armId = armId;
    task.count = count;
    task.pointer = 7;
    task.payload = payload;
    task.defaultNoop = defaultNoop;
    return task;
}

} // namespace

class MotionPlannerTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesDefaultPayload()
    {
        const auto result = MotionPlanner::parseArmPayload(QByteArray("(1000,0)E"));

        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(result.payload.flag, 1000);
        QCOMPARE(result.payload.armId, 1);
        QCOMPARE(result.payload.count, 0);
        QVERIFY(result.payload.defaultNoop);
        QCOMPARE(result.planned.task.armId, 1);
        QCOMPARE(result.planned.task.count, 0);
        QVERIFY(result.planned.task.defaultNoop);
        QVERIFY(result.planned.segments.isEmpty());
    }

    void parsesCameraPayloadValues()
    {
        const auto result = MotionPlanner::parseArmPayload(
            QByteArray("(2001,123,0.49,0.14,0.44,-1.14,0,-1.57)E"));

        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(result.payload.flag, 2001);
        QCOMPARE(result.payload.armId, 2);
        QCOMPARE(result.payload.count, 123);
        QCOMPARE(result.payload.values.size(), 6);
        QCOMPARE(result.payload.values.at(0), 0.49);
        QCOMPARE(result.payload.values.at(5), -1.57);
        QCOMPARE(result.planned.task.armId, 2);
        QCOMPARE(result.planned.task.count, 123);
        QVERIFY(!result.planned.task.defaultNoop);
        QCOMPARE(result.planned.task.payload, QByteArray("(2001,123,0.49,0.14,0.44,-1.14,0,-1.57)E"));
    }

    void rejectsInvalidFrame()
    {
        const auto result = MotionPlanner::parseArmPayload(QByteArray("1001,123)E"));

        QVERIFY(!result.ok);
        QVERIFY(result.message.contains(QStringLiteral("expected (...)E frame")));
    }

    void rejectsInvalidArmFlag()
    {
        const auto result = MotionPlanner::parseArmPayload(QByteArray("(3001,123,0.1)E"));

        QVERIFY(!result.ok);
        QVERIFY(result.message.contains(QStringLiteral("flag does not map")));
    }

    void rejectsNonNumericMotionValue()
    {
        const auto result = MotionPlanner::parseArmPayload(QByteArray("(1001,123,x)E"));

        QVERIFY(!result.ok);
        QVERIFY(result.message.contains(QStringLiteral("motion value is not numeric")));
    }

    void plansDefaultNoopWithoutRecipe()
    {
        const auto result = MotionPlanner::plan(
            taskForPayload(1, 0, QByteArray("(1000,0)E"), true),
            MotionRecipe{});

        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(result.planned.task.defaultNoop);
        QVERIFY(result.planned.segments.isEmpty());
    }

    void plansExpectedSpraySequence()
    {
        const auto result = MotionPlanner::plan(
            taskForPayload(1, 123, QByteArray("(1001,123,0.49,0.14,0.44,-1.14,0,-1.57)E")),
            recipeForArm(1));

        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(result.planned.segments.size(), 4);
        QCOMPARE(result.planned.segments.at(0).type, MotionSegmentType::MoveJPose2);
        QCOMPARE(result.planned.segments.at(0).velocity, 0.5);
        QCOMPARE(result.planned.segments.at(0).pose.x, 0.49);
        QCOMPARE(result.planned.segments.at(0).tool, QStringLiteral("spray_tool"));
        QCOMPARE(result.planned.segments.at(1).type, MotionSegmentType::ToolDigitalOut);
        QCOMPARE(result.planned.segments.at(1).ioChannel, 2);
        QVERIFY(result.planned.segments.at(1).ioValue);
        QCOMPARE(result.planned.segments.at(2).type, MotionSegmentType::MoveL);
        QCOMPARE(result.planned.segments.at(2).velocity, 0.25);
        QCOMPARE(result.planned.segments.at(3).type, MotionSegmentType::ToolDigitalOut);
        QVERIFY(!result.planned.segments.at(3).ioValue);
    }

    void rejectsCountMismatch()
    {
        const auto result = MotionPlanner::plan(
            taskForPayload(1, 124, QByteArray("(1001,123,0.49,0.14,0.44,-1.14,0,-1.57)E")),
            recipeForArm(1));

        QVERIFY(!result.ok);
        QVERIFY(result.message.contains(QStringLiteral("payload count does not match")));
    }

    void rejectsDisabledRecipe()
    {
        const auto result = MotionPlanner::plan(
            taskForPayload(1, 123, QByteArray("(1001,123,0.49,0.14,0.44,-1.14,0,-1.57)E")),
            MotionRecipe{});

        QVERIFY(!result.ok);
        QVERIFY(result.message.contains(QStringLiteral("recipe is disabled")));
    }
};

QObject* createMotionPlannerTest()
{
    return new MotionPlannerTest();
}

#include "test_motion_planner.moc"
