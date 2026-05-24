#include "test_motion_recipe_table.h"

#include "robot/MotionRecipeTable.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using spray::robot::MotionRecipeTable;
using spray::robot::MotionSegmentType;

namespace {

QString writeRecipes(QTemporaryDir& dir, const QByteArray& body)
{
    const QString path = dir.filePath(QStringLiteral("motion_recipes.toml"));
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Text);
    Q_ASSERT(opened);
    file.write(body);
    return path;
}

QByteArray validRecipes()
{
    return R"toml(
[[recipes]]
arm_id = 1
enabled = true
tool = "spray_tool_1"
wobj = "station"
q_near = [0, 0, 0, 0, 0, 0]
pose_indices = [0, 1, 2, 3, 4, 5]
approach_speed = 0.5
line_speed = 0.25
acceleration = 0.8
radius = 0.01
spray_io = "tool"
spray_io_channel = 2

[[recipes]]
arm_id = 2
enabled = true
tool = "spray_tool_2"
wobj = "station"
q_near = [1, 2, 3, 4, 5, 6]
pose_indices = [6, 7, 8, 9, 10, 11]
approach_speed = 0.6
line_speed = 0.3
acceleration = 0.9
radius = 0.02
spray_io = "standard"
spray_io_channel = 4
)toml";
}

} // namespace

class MotionRecipeTableTest final : public QObject {
    Q_OBJECT

private slots:
    void loadsValidArmRecipes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString error;
        const auto table = MotionRecipeTable::load(writeRecipes(dir, validRecipes()), &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(table.recipes().size(), 2);
        const auto* arm1 = table.recipeForArm(1);
        QVERIFY(arm1 != nullptr);
        QVERIFY(arm1->enabled);
        QCOMPARE(arm1->tool, QStringLiteral("spray_tool_1"));
        QCOMPARE(arm1->poseValueIndices, QList<int>({0, 1, 2, 3, 4, 5}));
        QCOMPARE(arm1->sprayIoType, MotionSegmentType::ToolDigitalOut);
        QCOMPARE(arm1->sprayIoChannel, 2);

        const auto* arm2 = table.recipeForArm(2);
        QVERIFY(arm2 != nullptr);
        QCOMPARE(arm2->sprayIoType, MotionSegmentType::StandardDigitalOut);
        QCOMPARE(arm2->qNear.j6, 6.0);
    }

    void rejectsDuplicateArm()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QByteArray text = validRecipes();
        text.replace("arm_id = 2", "arm_id = 1");
        QString error;
        MotionRecipeTable::load(writeRecipes(dir, text), &error);

        QVERIFY(error.contains(QStringLiteral("duplicate motion recipe arm")));
    }

    void rejectsMissingArm()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QString error;
        MotionRecipeTable::load(writeRecipes(dir, R"toml(
[[recipes]]
arm_id = 1
enabled = true
pose_indices = [0, 1, 2, 3, 4, 5]
)toml"), &error);

        QVERIFY(error.contains(QStringLiteral("missing motion recipe")));
    }

    void rejectsInvalidPoseMapping()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QByteArray text = validRecipes();
        text.replace("pose_indices = [0, 1, 2, 3, 4, 5]", "pose_indices = [0, 1, 2]");
        QString error;
        MotionRecipeTable::load(writeRecipes(dir, text), &error);

        QVERIFY(error.contains(QStringLiteral("pose_indices must contain 6")));
    }

    void rejectsNegativePoseMapping()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QByteArray text = validRecipes();
        text.replace("pose_indices = [0, 1, 2, 3, 4, 5]", "pose_indices = [0, 1, 2, 3, 4, -1]");
        QString error;
        MotionRecipeTable::load(writeRecipes(dir, text), &error);

        QVERIFY(error.contains(QStringLiteral("pose_indices must be non-negative")));
    }

    void rejectsInvalidSpeed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QByteArray text = validRecipes();
        text.replace("line_speed = 0.25", "line_speed = 0");
        QString error;
        MotionRecipeTable::load(writeRecipes(dir, text), &error);

        QVERIFY(error.contains(QStringLiteral("speed is invalid")));
    }

    void rejectsInvalidSprayIo()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QByteArray text = validRecipes();
        text.replace("spray_io = \"tool\"", "spray_io = \"relay\"");
        QString error;
        MotionRecipeTable::load(writeRecipes(dir, text), &error);

        QVERIFY(error.contains(QStringLiteral("spray_io must be tool or standard")));
    }

    void rejectsInvalidSprayIoChannel()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QByteArray text = validRecipes();
        text.replace("spray_io_channel = 2", "spray_io_channel = 0");
        QString error;
        MotionRecipeTable::load(writeRecipes(dir, text), &error);

        QVERIFY(error.contains(QStringLiteral("spray IO is invalid")));
    }
};

QObject* createMotionRecipeTableTest()
{
    return new MotionRecipeTableTest();
}

#include "test_motion_recipe_table.moc"
